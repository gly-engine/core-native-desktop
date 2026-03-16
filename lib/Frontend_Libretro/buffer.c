#include <string.h>
#include <stdint.h>
#include "buffer.h"

// ---------------------------------------------------------------------------
// BGRX -> RGBA (RETRO_PIXEL_FORMAT_XRGB8888)
//   Memory layout in LE: [B, G, R, 0x00] per pixel
//   Target layout:       [R, G, B, 0xFF] per pixel
// ---------------------------------------------------------------------------

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

static void copy_bgrx_to_rgba_neon(
    uint8_t *dst, int dst_stride,
    const uint8_t *src, int src_stride,
    int w, int h)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *s = src + y * src_stride;
        uint8_t       *d = dst + y * dst_stride;
        int x = 0;
        // vld4_u8: load 8 pixels (32 bytes), deinterleave into 4 channels
        for (; x <= w - 8; x += 8, s += 32, d += 32) {
            // XRGB8888 LE bytes: [B, G, R, X] per pixel
            // vld4_u8 deinterleaves: val[0]=B, val[1]=G, val[2]=R, val[3]=X
            uint8x8x4_t bgrx = vld4_u8(s);
            uint8x8x4_t rgba;
            rgba.val[0] = bgrx.val[2];    // R
            rgba.val[1] = bgrx.val[1];    // G
            rgba.val[2] = bgrx.val[0];    // B
            rgba.val[3] = vdup_n_u8(255); // A = 255
            vst4_u8(d, rgba);
        }
        for (; x < w; x++, s += 4, d += 4) {
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
    }
}

#elif defined(__SSSE3__)
#include <tmmintrin.h>

static void copy_bgrx_to_rgba_ssse3(
    uint8_t *dst, int dst_stride,
    const uint8_t *src, int src_stride,
    int w, int h)
{
    // Input  bytes: B0 G0 R0 X0  B1 G1 R1 X1  B2 G2 R2 X2  B3 G3 R3 X3
    // Output bytes: R0 G0 B0 FF  R1 G1 B1 FF  R2 G2 B2 FF  R3 G3 B3 FF
    // _mm_set_epi8(byte15, byte14, ..., byte1, byte0)
    const __m128i shuf = _mm_set_epi8(
        -1, 12, 13, 14,   // out 15-12: A3(->FF), B3, G3, R3
        -1,  8,  9, 10,   // out 11- 8: A2(->FF), B2, G2, R2
        -1,  4,  5,  6,   // out  7- 4: A1(->FF), B1, G1, R1
        -1,  0,  1,  2    // out  3- 0: A0(->FF), B0, G0, R0
    );
    const __m128i alpha_mask = _mm_set1_epi32((int)0xFF000000u);

    for (int y = 0; y < h; y++) {
        const uint8_t *s = src + y * src_stride;
        uint8_t       *d = dst + y * dst_stride;
        int x = 0;
        for (; x <= w - 4; x += 4, s += 16, d += 16) {
            __m128i v = _mm_loadu_si128((const __m128i *)s);
            v = _mm_shuffle_epi8(v, shuf);   // reorder channels (alpha slot -> 0)
            v = _mm_or_si128(v, alpha_mask); // set alpha = 0xFF
            _mm_storeu_si128((__m128i *)d, v);
        }
        for (; x < w; x++, s += 4, d += 4) {
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
    }
}

#endif

static void copy_bgrx_to_rgba_scalar(
    uint8_t *dst, int dst_stride,
    const uint8_t *src, int src_stride,
    int w, int h)
{
    for (int y = 0; y < h; y++) {
        const uint32_t *s = (const uint32_t *)(src + y * src_stride);
        uint32_t       *d = (uint32_t *)(dst + y * dst_stride);
        for (int x = 0; x < w; x++) {
            uint32_t px = s[x]; // 0x00RRGGBB
            d[x] = 0xFF000000u
                 | ((px & 0x000000FFu) << 16)  // B -> R position
                 |  (px & 0x0000FF00u)          // G stays
                 | ((px & 0x00FF0000u) >> 16);  // R -> B position
        }
    }
}

void libretro_copy_xrgb8888(uint8_t *dst, int dst_stride,
                             const uint8_t *src, int src_stride,
                             int w, int h)
{
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    copy_bgrx_to_rgba_neon(dst, dst_stride, src, src_stride, w, h);
#elif defined(__SSSE3__)
    copy_bgrx_to_rgba_ssse3(dst, dst_stride, src, src_stride, w, h);
#else
    copy_bgrx_to_rgba_scalar(dst, dst_stride, src, src_stride, w, h);
#endif
}

// ---------------------------------------------------------------------------
// RGB565 copy (no conversion, just handle pitch != linesize)
// ---------------------------------------------------------------------------

void libretro_copy_rgb565(uint8_t *dst, int dst_stride,
                          const uint8_t *src, int src_stride,
                          int w, int h)
{
    if (src_stride == dst_stride) {
        memcpy(dst, src, (size_t)h * (size_t)src_stride);
        return;
    }
    int row_bytes = w * 2;
    for (int y = 0; y < h; y++)
        memcpy(dst + y * dst_stride, src + y * src_stride, (size_t)row_bytes);
}
