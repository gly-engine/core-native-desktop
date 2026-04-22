#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "gecnd.h"

#if defined(_WIN32)
#  include <malloc.h>
#  define bg_aligned_alloc(align, size) _aligned_malloc((size), (align))
#  define bg_aligned_free(ptr)          _aligned_free(ptr)
#else
static inline void *bg_aligned_alloc(size_t align, size_t size) {
    void *ptr = NULL;
    if (posix_memalign(&ptr, align, size) != 0) return NULL;
    return ptr;
}
#  define bg_aligned_free(ptr) free(ptr)
#endif

static atomic_int g_owner = 0;

bool gamely_daemon_media_background_claim(void) {
    int expected = 0;
    return atomic_compare_exchange_strong(&g_owner, &expected, 1);
}

void gamely_daemon_media_background_release(void) {
    atomic_store(&g_owner, 0);
}

static MediaFrame  frames[3]       = {0};
static atomic_int  front_idx       = 0;
static atomic_int  back_idx        = 1;
static atomic_int  mid_idx         = 2;
static atomic_int  mid_dirty       = 0;
static atomic_int  update_counter  = 0;

static void frame_resize(MediaFrame *f, int w, int h, int format) {
    if (f->width == w && f->height == h && f->format == format && f->data[0])
        return;
    if (f->data[0]) bg_aligned_free(f->data[0]);
    memset(f, 0, sizeof(MediaFrame));
    f->width  = w;
    f->height = h;
    f->format = format;
    size_t total;
    if (format == GECND_PIX_FMT_YUV420P) {
        f->linesize[0] = w;
        f->linesize[1] = w / 2;
        f->linesize[2] = w / 2;
        total = (size_t)(w * h) + (size_t)(w / 2 * h / 2) * 2;
    } else {
        int bpp = (format == GECND_PIX_FMT_RGB565) ? 2 : 4;
        f->linesize[0] = w * bpp;
        total = (size_t)(w * h * bpp);
    }
    f->data[0] = bg_aligned_alloc(16, total);
    if (format == GECND_PIX_FMT_YUV420P) {
        f->data[1] = f->data[0] + (w * h);
        f->data[2] = f->data[1] + (w / 2 * h / 2);
    }
    atomic_store(&f->ready, false);
}

static void buffer_resize(int w, int h, int format) {
    frame_resize(&frames[0], w, h, format);
    frame_resize(&frames[1], w, h, format);
    frame_resize(&frames[2], w, h, format);
}

static MediaFrame *buffer_get_back(void) {
    return &frames[atomic_load(&back_idx)];
}

static void buffer_swap(void) {
    int b = atomic_load(&back_idx);
    int m = atomic_exchange(&mid_idx, b);
    atomic_store(&back_idx, m);
    atomic_store(&mid_dirty, 1);
    atomic_fetch_add(&update_counter, 1);
}

MediaFrame *gamely_daemon_media_background_get_frame(void) {
    if (atomic_load(&mid_dirty)) {
        int f = atomic_load(&front_idx);
        int m = atomic_exchange(&mid_idx, f);
        atomic_store(&front_idx, m);
        atomic_store(&mid_dirty, 0);
    }
    MediaFrame *f = &frames[atomic_load(&front_idx)];
    if (atomic_load(&f->ready)) return f;
    return NULL;
}

bool gamely_daemon_media_background_check_update(atomic_int *local_counter) {
    int current = atomic_load(&update_counter);
    if (current != atomic_load(local_counter)) {
        atomic_store(local_counter, current);
        return true;
    }
    return false;
}

static void background_free(void) {
    for (int i = 0; i < 3; i++) {
        if (frames[i].data[0]) bg_aligned_free(frames[i].data[0]);
    }
    memset(frames, 0, sizeof(frames));
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
static void copy_bgrx_neon(uint8_t *dst, int ds, const uint8_t *src, int ss, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint8_t *s = src + y * ss;
        uint8_t       *d = dst + y * ds;
        int x = 0;
        for (; x <= w - 8; x += 8, s += 32, d += 32) {
            uint8x8x4_t bgrx = vld4_u8(s);
            uint8x8x4_t rgba;
            rgba.val[0] = bgrx.val[2];
            rgba.val[1] = bgrx.val[1];
            rgba.val[2] = bgrx.val[0];
            rgba.val[3] = vdup_n_u8(255);
            vst4_u8(d, rgba);
        }
        for (; x < w; x++, s += 4, d += 4) { d[0]=s[2]; d[1]=s[1]; d[2]=s[0]; d[3]=255; }
    }
}
#elif defined(__SSSE3__)
#include <tmmintrin.h>
static void copy_bgrx_ssse3(uint8_t *dst, int ds, const uint8_t *src, int ss, int w, int h) {
    const __m128i shuf = _mm_set_epi8(-1,12,13,14, -1,8,9,10, -1,4,5,6, -1,0,1,2);
    const __m128i amask = _mm_set1_epi32((int)0xFF000000u);
    for (int y = 0; y < h; y++) {
        const uint8_t *s = src + y * ss;
        uint8_t       *d = dst + y * ds;
        int x = 0;
        for (; x <= w - 4; x += 4, s += 16, d += 16) {
            __m128i v = _mm_loadu_si128((const __m128i *)s);
            v = _mm_shuffle_epi8(v, shuf);
            v = _mm_or_si128(v, amask);
            _mm_storeu_si128((__m128i *)d, v);
        }
        for (; x < w; x++, s += 4, d += 4) { d[0]=s[2]; d[1]=s[1]; d[2]=s[0]; d[3]=255; }
    }
}
#endif

static void copy_bgrx_scalar(uint8_t *dst, int ds, const uint8_t *src, int ss, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint32_t *s = (const uint32_t *)(src + y * ss);
        uint32_t       *d = (uint32_t *)(dst + y * ds);
        for (int x = 0; x < w; x++) {
            uint32_t p = s[x];
            d[x] = 0xFF000000u | ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p & 0xFF0000u) >> 16);
        }
    }
}

static void do_copy_xrgb8888(uint8_t *dst, int ds, const uint8_t *src, int ss, int w, int h) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    copy_bgrx_neon(dst, ds, src, ss, w, h);
#elif defined(__SSSE3__)
    copy_bgrx_ssse3(dst, ds, src, ss, w, h);
#else
    copy_bgrx_scalar(dst, ds, src, ss, w, h);
#endif
}

void gamely_daemon_media_background_push_xrgb8888(const uint8_t *data, int w, int h, int pitch) {
    buffer_resize(w, h, GECND_PIX_FMT_RGBA8888);
    MediaFrame *f = buffer_get_back();
    if (!f || !f->data[0]) return;
    do_copy_xrgb8888(f->data[0], f->linesize[0], data, pitch, w, h);
    atomic_store(&f->ready, true);
    buffer_swap();
}

void gamely_daemon_media_background_push_rgb565(const uint8_t *data, int w, int h, int pitch) {
    buffer_resize(w, h, GECND_PIX_FMT_RGB565);
    MediaFrame *f = buffer_get_back();
    if (!f || !f->data[0]) return;
    if (pitch == f->linesize[0]) {
        memcpy(f->data[0], data, (size_t)(pitch * h));
    } else {
        int row = w * 2;
        for (int y = 0; y < h; y++)
            memcpy(f->data[0] + y * f->linesize[0], data + y * pitch, (size_t)row);
    }
    atomic_store(&f->ready, true);
    buffer_swap();
}

void gamely_daemon_media_background_push_yuv420(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                                                 int w, int h, int y_stride, int uv_stride) {
    buffer_resize(w, h, GECND_PIX_FMT_YUV420P);
    MediaFrame *f = buffer_get_back();
    if (!f || !f->data[0]) return;
    int uv_h = h / 2;
    if (y_stride == f->linesize[0]) {
        memcpy(f->data[0], y, (size_t)(y_stride * h));
    } else {
        for (int i = 0; i < h; i++)
            memcpy(f->data[0] + i * f->linesize[0], y + i * y_stride, (size_t)f->linesize[0]);
    }
    if (uv_stride == f->linesize[1]) {
        memcpy(f->data[1], u, (size_t)(uv_stride * uv_h));
        memcpy(f->data[2], v, (size_t)(uv_stride * uv_h));
    } else {
        for (int i = 0; i < uv_h; i++) {
            memcpy(f->data[1] + i * f->linesize[1], u + i * uv_stride, (size_t)f->linesize[1]);
            memcpy(f->data[2] + i * f->linesize[2], v + i * uv_stride, (size_t)f->linesize[2]);
        }
    }
    atomic_store(&f->ready, true);
    buffer_swap();
}

void gamely_daemon_media_init(void) {
}

void gamely_daemon_media_shutdown(void) {
    background_free();
}
