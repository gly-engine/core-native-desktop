#include <stdlib.h>
#include <spng.h>
#include "gecnd.h"

static gamely_img_decoded_t spng_decode_cb(const uint8_t *data, size_t len) {
    gamely_img_decoded_t out = {0};

    spng_ctx *ctx = spng_ctx_new(0);
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
    out.w      = (int16_t)ihdr.width;
    out.h      = (int16_t)ihdr.height;
    return out;
}

gamely_img_decoded_t gamely_driver_decoder_stb(const uint8_t *data, size_t len);
/** 
 * @todo move this!
 */
void gamely_daemon_img_spng_register(void) {
    gamely_daemon_img_register_decoder("bmp", "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("gif", "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("png", "rgba", true, spng_decode_cb);
}
