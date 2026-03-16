#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include "gebuffer.h"

#if defined(_WIN32)
#  include <malloc.h>
#  define gecnd_aligned_alloc(align, size) _aligned_malloc((size), (align))
#  define gecnd_aligned_free(ptr)          _aligned_free(ptr)
#else
static inline void *gecnd_aligned_alloc(size_t align, size_t size) {
    void *ptr = NULL;
    if (posix_memalign(&ptr, align, size) != 0) return NULL;
    return ptr;
}
#  define gecnd_aligned_free(ptr) free(ptr)
#endif

// ---------------------------------------------------------------------------
// Triple buffer:
//   frames[front_idx] - consumer reads this
//   frames[back_idx]  - producer writes this
//   frames[mid_idx]   - exchange slot: holds the most recently completed frame
//
// On producer swap: back_idx <-> mid_idx, set mid_dirty=1
// On consumer read: if mid_dirty, swap front_idx <-> mid_idx, clear mid_dirty
//
// Producer never blocks. Consumer always sees the freshest available frame.
// ---------------------------------------------------------------------------

static MediaFrame frames[3] = {0};
static atomic_int  front_idx    = 0;
static atomic_int  back_idx     = 1;
static atomic_int  mid_idx      = 2;
static atomic_int  mid_dirty    = 0;
static atomic_int  update_counter = 0;

MediaFrame* gecnd_buffer_get_front(void) {
    return &frames[atomic_load(&front_idx)];
}

MediaFrame* gecnd_buffer_get_back(void) {
    return &frames[atomic_load(&back_idx)];
}

void gecnd_buffer_swap(void) {
    // Publish written back: swap back_idx <-> mid_idx
    int b = atomic_load(&back_idx);
    int m = atomic_exchange(&mid_idx, b);
    atomic_store(&back_idx, m);
    atomic_store(&mid_dirty, 1);
    gecnd_buffer_notify_update();
}

void gecnd_buffer_notify_update(void) {
    atomic_fetch_add(&update_counter, 1);
}

bool gecnd_buffer_check_update(atomic_int *local_counter) {
    int current = atomic_load(&update_counter);
    if (current != atomic_load(local_counter)) {
        atomic_store(local_counter, current);
        return true;
    }
    return false;
}

MediaFrame* gecnd_get_background_frame(void) {
    if (atomic_load(&mid_dirty)) {
        // Consume: swap front_idx <-> mid_idx so front gets the freshest frame
        int f = atomic_load(&front_idx);
        int m = atomic_exchange(&mid_idx, f);
        atomic_store(&front_idx, m);
        atomic_store(&mid_dirty, 0);
    }
    MediaFrame *f = &frames[atomic_load(&front_idx)];
    if (atomic_load(&f->ready)) return f;
    return NULL;
}

static void frame_resize(MediaFrame *f, int w, int h, int format) {
    if (f->width == w && f->height == h && f->format == format && f->data[0]) {
        return;
    }

    if (f->data[0]) {
        gecnd_aligned_free(f->data[0]);
    }

    memset(f, 0, sizeof(MediaFrame));
    f->width = w;
    f->height = h;
    f->format = format;

    size_t total_size = 0;
    if (format == GECND_PIX_FMT_YUV420P) {
        f->linesize[0] = w;
        f->linesize[1] = w / 2;
        f->linesize[2] = w / 2;
        total_size = (size_t)(w * h) + (size_t)(w / 2 * h / 2) * 2;
    } else {
        int bpp = (format == GECND_PIX_FMT_RGB565) ? 2 : 4;
        f->linesize[0] = w * bpp;
        total_size = (size_t)(w * h * bpp);
    }

    f->data[0] = gecnd_aligned_alloc(16, total_size);
    if (format == GECND_PIX_FMT_YUV420P) {
        f->data[1] = f->data[0] + (w * h);
        f->data[2] = f->data[1] + (w / 2 * h / 2);
    }

    atomic_store(&f->ready, false);
}

void gecnd_buffer_resize(int w, int h, int format) {
    frame_resize(&frames[0], w, h, format);
    frame_resize(&frames[1], w, h, format);
    frame_resize(&frames[2], w, h, format);
}

void gecnd_buffer_free(void) {
    for (int i = 0; i < 3; i++) {
        if (frames[i].data[0]) gecnd_aligned_free(frames[i].data[0]);
    }
    memset(frames, 0, sizeof(frames));
}
