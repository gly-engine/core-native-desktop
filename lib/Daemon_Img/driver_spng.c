#include <stdlib.h>
#include <spng.h>
#include "gecnd.h"

gamely_img_decoded_t gamely_driver_decoder_spng(const uint8_t *data, size_t len) {
    gamely_img_decoded_t out = {0};

    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32);
    if (!ctx) return out;

    if (spng_set_png_buffer(ctx, data, len) != 0) {
        spng_ctx_free(ctx);
        return out;
    }

    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0) {
        spng_ctx_free(ctx);
        return out;
    }

    size_t img_size = 0;
    if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &img_size) != 0) {
        spng_ctx_free(ctx);
        return out;
    }

    uint8_t *pixels = malloc(img_size);
    if (!pixels) {
        spng_ctx_free(ctx);
        return out;
    }

    if (spng_decode_image(ctx, pixels, img_size, SPNG_FMT_RGBA8, 0) != 0) {
        free(pixels);
        spng_ctx_free(ctx);
        return out;
    }

    spng_ctx_free(ctx);
    out.pixels = pixels;
    out.len    = img_size;
    out.w      = (int16_t)ihdr.width;
    out.h      = (int16_t)ihdr.height;
    return out;
}
