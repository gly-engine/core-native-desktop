#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

#include "gecnd.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define GE_HAS_NEON 1
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#define GE_HAS_SSE2 1
#endif

static void row_halve(uint8_t *dst, const uint8_t *src, int dst_w) {
    int x = 0;
#if defined(GE_HAS_NEON)
    for (; x + 16 <= dst_w; x += 16)
        vst1q_u8(dst + x, vld2q_u8(src + (size_t)2 * x).val[0]);
#elif defined(GE_HAS_SSE2)
    const __m128i evens = _mm_set1_epi16(0x00FF);
    for (; x + 16 <= dst_w; x += 16) {
        __m128i a = _mm_and_si128(_mm_loadu_si128((const __m128i *)(src + 2 * x)),      evens);
        __m128i b = _mm_and_si128(_mm_loadu_si128((const __m128i *)(src + 2 * x + 16)), evens);
        _mm_storeu_si128((__m128i *)(dst + x), _mm_packus_epi16(a, b));
    }
#endif
    for (; x < dst_w; x++) dst[x] = src[2 * x];
}

static void row_double(uint8_t *dst, const uint8_t *src, int dst_w) {
    int x = 0;
#if defined(GE_HAS_NEON)
    for (; x + 16 <= dst_w; x += 16) {
        uint8x8_t   s = vld1_u8(src + (x >> 1));
        uint8x8x2_t z = vzip_u8(s, s);
        vst1q_u8(dst + x, vcombine_u8(z.val[0], z.val[1]));
    }
#elif defined(GE_HAS_SSE2)
    for (; x + 16 <= dst_w; x += 16) {
        __m128i s = _mm_loadl_epi64((const __m128i *)(src + (x >> 1)));
        _mm_storeu_si128((__m128i *)(dst + x), _mm_unpacklo_epi8(s, s));
    }
#endif
    for (; x < dst_w; x++) dst[x] = src[x >> 1];
}

static void row_nearest(uint8_t *dst, const uint8_t *src, int dst_w, int src_w) {
    for (int x = 0; x < dst_w; x++) dst[x] = src[(int)((long)x * src_w / dst_w)];
}

static void resample_chroma(uint8_t *dst, const uint8_t *src,
                            int dst_w, int dst_h,
                            int src_w, int src_h, int src_stride) {
    if (src_w == dst_w && src_h == dst_h && src_stride == src_w) {
        memcpy(dst, src, (size_t)dst_w * dst_h);
        return;
    }
    for (int y = 0; y < dst_h; y++) {
        int sy = (src_h == dst_h)     ? y
               : (src_h == 2 * dst_h) ? 2 * y
               : (int)((long)y * src_h / dst_h);
        const uint8_t *srow = src + (size_t)sy * src_stride;
        uint8_t       *drow = dst + (size_t)y * dst_w;
        if      (src_w == dst_w)     memcpy(drow, srow, (size_t)dst_w);
        else if (src_w == 2 * dst_w) row_halve (drow, srow, dst_w);
        else if (2 * src_w == dst_w) row_double(drow, srow, dst_w);
        else                         row_nearest(drow, srow, dst_w, src_w);
    }
}

gamely_img_decoded_t gamely_driver_decoder_jpegturbo(const uint8_t *data, size_t len) {
    gamely_img_decoded_t out = {0};

    tjhandle tj = tjInitDecompress();
    if (!tj) { fprintf(stderr, "[jpegturbo] tjInitDecompress failed\n"); return out; }

    unsigned char *nat = NULL;
    uint8_t       *dst = NULL;

    do {
        int w = 0, h = 0, subsamp = 0, colorspace = 0;
        if (tjDecompressHeader3(tj, data, (unsigned long)len,
                                &w, &h, &subsamp, &colorspace) != 0) {
            fprintf(stderr, "[jpegturbo] header failed (len=%zu): %s\n",
                    len, tjGetErrorStr2(tj));
            break;
        }
        if (w <= 0 || h <= 0) {
            fprintf(stderr, "[jpegturbo] bad dims %dx%d\n", w, h);
            break;
        }

        const int yw = w & ~1, yh = h & ~1;
        if (yw <= 0 || yh <= 0) {
            fprintf(stderr, "[jpegturbo] too small after even-align %dx%d\n", w, h);
            break;
        }
        const int cw = yw / 2, ch = yh / 2;
        const int gray = (subsamp == TJSAMP_GRAY);

        const int ny_w = tjPlaneWidth (0, w, subsamp);
        const int ny_h = tjPlaneHeight(0, h, subsamp);
        const int nc_w = gray ? 0 : tjPlaneWidth (1, w, subsamp);
        const int nc_h = gray ? 0 : tjPlaneHeight(1, h, subsamp);
        if (ny_w < yw || ny_h < yh) {
            fprintf(stderr, "[jpegturbo] luma plane %dx%d < output %dx%d (subsamp=%d)\n",
                    ny_w, ny_h, yw, yh, subsamp);
            break;
        }

        const size_t y_size = (size_t)yw * yh;
        const size_t c_size = (size_t)cw * ch;
        dst = malloc(y_size + 2 * c_size);
        if (!dst) { fprintf(stderr, "[jpegturbo] oom dst %zu\n", y_size + 2 * c_size); break; }

        uint8_t *dst_y = dst;
        uint8_t *dst_u = dst + y_size;
        uint8_t *dst_v = dst + y_size + c_size;

        if (subsamp == TJSAMP_420 && w == yw && h == yh) {
            unsigned char *planes[3] = { dst_y, dst_u, dst_v };
            if (tjDecompressToYUVPlanes(tj, data, (unsigned long)len,
                                        planes, w, NULL, h, TJFLAG_FASTDCT) != 0) {
                fprintf(stderr, "[jpegturbo] decode (direct 420 %dx%d): %s\n",
                        w, h, tjGetErrorStr2(tj));
                break;
            }
        } else {
            const unsigned long sz0 = tjPlaneSizeYUV(0, w, 0, h, subsamp);
            const unsigned long sz1 = gray ? 0 : tjPlaneSizeYUV(1, w, 0, h, subsamp);
            const unsigned long sz2 = gray ? 0 : tjPlaneSizeYUV(2, w, 0, h, subsamp);
            if (sz0 == (unsigned long)-1) {
                fprintf(stderr, "[jpegturbo] tjPlaneSizeYUV failed (subsamp=%d): %s\n",
                        subsamp, tjGetErrorStr2(tj));
                break;
            }

            nat = malloc(sz0 + sz1 + sz2);
            if (!nat) { fprintf(stderr, "[jpegturbo] oom nat %lu\n", sz0 + sz1 + sz2); break; }

            unsigned char *planes[3] = { nat, nat + sz0, nat + sz0 + sz1 };
            if (tjDecompressToYUVPlanes(tj, data, (unsigned long)len,
                                        planes, w, NULL, h, TJFLAG_FASTDCT) != 0) {
                fprintf(stderr, "[jpegturbo] decode (%dx%d subsamp=%d): %s\n",
                        w, h, subsamp, tjGetErrorStr2(tj));
                break;
            }

            if (ny_w == yw)
                memcpy(dst_y, planes[0], y_size);
            else
                for (int y = 0; y < yh; y++)
                    memcpy(dst_y + (size_t)y * yw, planes[0] + (size_t)y * ny_w, (size_t)yw);

            if (gray) {
                memset(dst_u, 128, c_size);
                memset(dst_v, 128, c_size);
            } else {
                resample_chroma(dst_u, planes[1], cw, ch, nc_w, nc_h, nc_w);
                resample_chroma(dst_v, planes[2], cw, ch, nc_w, nc_h, nc_w);
            }
        }

        out.pixels = dst;
        out.len    = y_size + 2 * c_size;
        out.w      = (int16_t)yw;
        out.h      = (int16_t)yh;
        dst = NULL;
    } while (0);

    free(dst);
    free(nat);
    tjDestroy(tj);
    return out;
}
