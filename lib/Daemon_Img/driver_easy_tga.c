#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define GE_HAS_NEON 1
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#define GE_HAS_SSE2 1
#endif

/* TGA passthrough decoder (no library). Output format follows the colour
 * sample width (the pixel for truecolor, the palette entry for color-mapped):
 *   - 16bpp (native A1R5G5B5) -> RGBA5551 (2 B/px)
 *   - 24/32bpp (BGR / BGRA)   -> RGBA8888 (4 B/px), full 8-bit color/alpha
 *
 * Truecolor uncompressed (type 2) / RLE (type 10) and color-mapped
 * uncompressed (type 1) / RLE (type 9). The truecolor uncompressed,
 * non-h-flipped case converts whole scanlines with NEON/SSE2; color-mapped,
 * RLE and the rare horizontal flip fall back to the scalar path. Color-mapped
 * pixels carry 8- or 16-bit indices resolved against the file's palette.
 * Origin bits are honoured so the result is top-left. Owner frees `data` after
 * return. */

/* ── single-pixel converters (scalar / RLE / h-flip path) ─────────── */

static inline uint16_t to_5551(const uint8_t *p, int alpha16) {
    uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);  /* A1R5G5B5 LE */
    /* R5G5B5A1 = rotate-left 1; attr bit is alpha only when declared. */
    return (uint16_t)((v << 1) | (alpha16 ? (v >> 15) : 1u));
}

static inline void to_8888(const uint8_t *p, int bits, uint8_t *o) {
    o[0] = p[2];  o[1] = p[1];  o[2] = p[0];              /* BGR -> RGB */
    o[3] = (bits == 32) ? p[3] : 255;
}

/* ── per-scanline converters (SIMD fast path) ─────────────────────── */

static void row_16_to_5551(uint16_t *d, const uint16_t *s, int n, int alpha16) {
    int x = 0;
#if defined(GE_HAS_NEON)
    uint16x8_t one = vdupq_n_u16(1);
    for (; x + 8 <= n; x += 8) {
        uint16x8_t v = vld1q_u16(s + x);
        uint16x8_t a = alpha16 ? vshrq_n_u16(v, 15) : one;
        vst1q_u16(d + x, vorrq_u16(vshlq_n_u16(v, 1), a));
    }
#elif defined(GE_HAS_SSE2)
    __m128i one = _mm_set1_epi16(1);
    for (; x + 8 <= n; x += 8) {
        __m128i v = _mm_loadu_si128((const __m128i *)(s + x));
        __m128i a = alpha16 ? _mm_srli_epi16(v, 15) : one;
        _mm_storeu_si128((__m128i *)(d + x), _mm_or_si128(_mm_slli_epi16(v, 1), a));
    }
#endif
    for (; x < n; x++) {
        uint16_t v = s[x];
        d[x] = (uint16_t)((v << 1) | (alpha16 ? (v >> 15) : 1u));
    }
}

static void row_24_to_8888(uint8_t *d, const uint8_t *s, int n) {
    int x = 0;
#if defined(GE_HAS_NEON)
    uint8x16_t a255 = vdupq_n_u8(255);
    for (; x + 16 <= n; x += 16) {
        uint8x16x3_t bgr = vld3q_u8(s + (size_t)x * 3);
        uint8x16x4_t o;
        o.val[0] = bgr.val[2]; o.val[1] = bgr.val[1]; o.val[2] = bgr.val[0]; o.val[3] = a255;
        vst4q_u8(d + (size_t)x * 4, o);
    }
#endif
    for (; x < n; x++) {
        const uint8_t *p = s + (size_t)x * 3; uint8_t *o = d + (size_t)x * 4;
        o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255;
    }
}

static void row_32_to_8888(uint8_t *d, const uint8_t *s, int n) {
    int x = 0;
#if defined(GE_HAS_NEON)
    for (; x + 16 <= n; x += 16) {
        uint8x16x4_t bgra = vld4q_u8(s + (size_t)x * 4);
        uint8x16x4_t o;
        o.val[0] = bgra.val[2]; o.val[1] = bgra.val[1]; o.val[2] = bgra.val[0]; o.val[3] = bgra.val[3];
        vst4q_u8(d + (size_t)x * 4, o);
    }
#elif defined(GE_HAS_SSE2)
    /* per 32-bit lane bytes [B,G,R,A] -> [R,G,B,A]: swap byte0 and byte2. */
    for (; x + 4 <= n; x += 4) {
        __m128i v = _mm_loadu_si128((const __m128i *)(s + (size_t)x * 4));
        __m128i b  = _mm_and_si128(v, _mm_set1_epi32(0x000000FF));
        __m128i r  = _mm_and_si128(v, _mm_set1_epi32(0x00FF0000));
        __m128i ga = _mm_and_si128(v, _mm_set1_epi32((int)0xFF00FF00));
        __m128i out = _mm_or_si128(ga, _mm_or_si128(_mm_slli_epi32(b, 16), _mm_srli_epi32(r, 16)));
        _mm_storeu_si128((__m128i *)(d + (size_t)x * 4), out);
    }
#endif
    for (; x < n; x++) {
        const uint8_t *p = s + (size_t)x * 4; uint8_t *o = d + (size_t)x * 4;
        o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3];
    }
}

/* ── scalar destination addressing (RLE / horizontal flip) ────────── */

static inline size_t dst_index(size_t i, int width, int height, int flip_h, int flip_v) {
    size_t col = i % (size_t)width, frow = i / (size_t)width;
    size_t oc   = flip_h ? ((size_t)width  - 1 - col)  : col;
    size_t orow = flip_v ? ((size_t)height - 1 - frow) : frow;
    return orow * (size_t)width + oc;
}

static inline void put_px(uint8_t *dst, size_t di, const uint8_t *src,
                          int depth, int alpha16, int out5551) {
    if (out5551) ((uint16_t *)dst)[di] = to_5551(src, alpha16);
    else         to_8888(src, depth, dst + di * 4);
}

/* Resolve a source unit to the colour sample to convert. Truecolor passes the
 * unit straight through; color-mapped reads the 8/16-bit index, applies the
 * palette's first-entry offset and returns the palette entry (or `zero` for an
 * out-of-range index). */
static inline const uint8_t *cmap_sample(const uint8_t *unit, int colormapped,
                                         int idx_bytes, const uint8_t *cmap,
                                         int cesz, unsigned cmap_len,
                                         unsigned first, const uint8_t *zero) {
    if (!colormapped) return unit;
    unsigned idx = (idx_bytes == 2) ? (unsigned)(unit[0] | (unit[1] << 8)) : unit[0];
    if (idx < first) return zero;
    idx -= first;
    if (idx >= cmap_len) return zero;
    return cmap + (size_t)idx * (size_t)cesz;
}

static gamely_img_decoded_t driver_decoder_tga(const uint8_t *data, size_t len) {
    gamely_img_decoded_t out = {0};
    if (!data || len < 18) return out;

    uint8_t  id_len   = data[0];
    uint8_t  cmap_typ = data[1];
    uint8_t  img_type = data[2];
    uint16_t cmap_first = (uint16_t)(data[3] | (data[4] << 8));
    uint16_t cmap_len = (uint16_t)(data[5] | (data[6] << 8));
    uint8_t  cmap_esz = data[7];
    uint16_t width    = (uint16_t)(data[12] | (data[13] << 8));
    uint16_t height   = (uint16_t)(data[14] | (data[15] << 8));
    uint8_t  depth    = data[16];
    uint8_t  desc     = data[17];

    const int colormapped = (img_type == 1 || img_type == 9);
    const int rle         = (img_type == 9 || img_type == 10);

    if (img_type != 1 && img_type != 2 && img_type != 9 && img_type != 10)
        return out;                                /* truecolor or color-mapped */
    if (colormapped) {
        if (cmap_typ != 1)                                  return out;
        if (depth != 8 && depth != 16)                      return out;  /* index size */
        if (cmap_esz != 15 && cmap_esz != 16 &&
            cmap_esz != 24 && cmap_esz != 32)               return out;
    } else if (depth != 16 && depth != 24 && depth != 32) {
        return out;
    }
    if (width == 0 || height == 0)                          return out;

    int    bpp        = depth / 8;                 /* source bytes/pixel (index or colour) */
    int    cesz       = (cmap_esz + 7) / 8;        /* palette entry bytes */
    size_t cmap_bytes = (cmap_typ == 1) ? (size_t)cmap_len * (size_t)cesz : 0;
    size_t off        = 18u + (size_t)id_len + cmap_bytes;
    if (off > len)                                          return out;

    const uint8_t *cmap   = data + 18u + (size_t)id_len;        /* palette base */
    const int sdepth  = colormapped ? cmap_esz : depth;        /* colour sample depth */
    const int out5551 = colormapped ? (cmap_esz <= 16) : (depth == 16);
    const int outbpp  = out5551 ? 2 : 4;
    const int alpha16 = (desc & 0x0f) ? 1 : 0;

    size_t   npx = (size_t)width * height;
    uint8_t *dst = malloc(npx * (size_t)outbpp);
    if (!dst) return out;

    const uint8_t *src = data + off;
    const uint8_t *end = data + len;
    int flip_v = !(desc & 0x20);   /* default origin bottom-left -> flip */
    int flip_h =  (desc & 0x10);

    static const uint8_t zero[4] = {0, 0, 0, 0};   /* out-of-range palette index */

    if (img_type == 2 && !flip_h) {
        /* fast path: convert whole scanlines, vertical flip = pick dst row. */
        if (src + npx * (size_t)bpp > end) { free(dst); return out; }
        for (int frow = 0; frow < height; frow++) {
            int orow = flip_v ? (height - 1 - frow) : frow;
            const uint8_t *srow = src + (size_t)frow * width * bpp;
            uint8_t       *drow = dst + (size_t)orow * width * outbpp;
            if      (out5551)     row_16_to_5551((uint16_t *)drow, (const uint16_t *)srow, width, alpha16);
            else if (depth == 24) row_24_to_8888(drow, srow, width);
            else                  row_32_to_8888(drow, srow, width);
        }
    } else if (!rle) {
        /* scalar uncompressed: truecolor h-flip, or color-mapped (any origin) */
        if (src + npx * (size_t)bpp > end) { free(dst); return out; }
        for (size_t i = 0; i < npx; i++) {
            const uint8_t *sp = cmap_sample(src + i * bpp, colormapped, bpp,
                                            cmap, cesz, cmap_len, cmap_first, zero);
            put_px(dst, dst_index(i, width, height, flip_h, flip_v),
                   sp, sdepth, alpha16, out5551);
        }
    } else {
        /* RLE (type 10 truecolor / type 9 color-mapped), sequential */
        size_t i = 0;
        while (i < npx) {
            if (src >= end) { free(dst); return out; }
            uint8_t hdr   = *src++;
            int     count = (hdr & 0x7f) + 1;
            if (hdr & 0x80) {                       /* run: one pixel repeated */
                if (src + bpp > end) { free(dst); return out; }
                const uint8_t *sp = cmap_sample(src, colormapped, bpp,
                                                cmap, cesz, cmap_len, cmap_first, zero);
                for (int k = 0; k < count && i < npx; k++, i++)
                    put_px(dst, dst_index(i, width, height, flip_h, flip_v),
                           sp, sdepth, alpha16, out5551);
                src += bpp;
            } else {                                /* raw: count pixels */
                if (src + (size_t)count * bpp > end) { free(dst); return out; }
                for (int k = 0; k < count && i < npx; k++, i++) {
                    const uint8_t *sp = cmap_sample(src, colormapped, bpp,
                                                    cmap, cesz, cmap_len, cmap_first, zero);
                    put_px(dst, dst_index(i, width, height, flip_h, flip_v),
                           sp, sdepth, alpha16, out5551);
                    src += bpp;
                }
            }
        }
    }

    out.pixels       = dst;
    out.len          = npx * (size_t)outbpp;
    out.w            = (int16_t)width;
    out.h            = (int16_t)height;
    out.color_format = out5551 ? GECND_PIX_FMT_RGBA5551 : GECND_PIX_FMT_RGBA8888;
    return out;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_decoder_sync:tga:rgba8888", driver_decoder_tga, NULL);
    gecnd_registry("set", "image_decoder_sync:tga:rgba5551", driver_decoder_tga, NULL);
}
