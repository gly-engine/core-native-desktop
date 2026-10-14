#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>

#include "gecnd.h"

static gamely_img_decoded_t driver_decoder_libwebp(const uint8_t *data, size_t len) {
    gamely_img_decoded_t out = {0};
    uint8_t             *dst = NULL;

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) {
        fprintf(stderr, "[libwebp] WebPInitDecoderConfig failed\n");
        return out;
    }

    do {
        VP8StatusCode st = WebPGetFeatures(data, len, &config.input);
        if (st != VP8_STATUS_OK) {
            fprintf(stderr, "[libwebp] header failed (len=%zu): status=%d\n", len, st);
            break;
        }
        if (config.input.has_animation) {
            fprintf(stderr, "[libwebp] animated webp not supported\n");
            break;
        }

        const int w = config.input.width;
        const int h = config.input.height;
        if (w <= 0 || h <= 0 || w > INT16_MAX || h > INT16_MAX) {
            fprintf(stderr, "[libwebp] bad dims %dx%d\n", w, h);
            break;
        }

        config.options.use_threads = 0;
        config.output.is_external_memory = 1;

        if (config.input.has_alpha) {
            const size_t size = (size_t)w * h * 4;
            dst = malloc(size);
            if (!dst) { fprintf(stderr, "[libwebp] oom dst %zu\n", size); break; }

            config.output.colorspace    = MODE_RGBA;
            config.output.u.RGBA.rgba   = dst;
            config.output.u.RGBA.stride = w * 4;
            config.output.u.RGBA.size   = size;

            st = WebPDecode(data, len, &config);
            if (st != VP8_STATUS_OK) {
                fprintf(stderr, "[libwebp] decode (rgba %dx%d): status=%d\n", w, h, st);
                break;
            }

            out.pixels       = dst;
            out.len          = size;
            out.w            = (int16_t)w;
            out.h            = (int16_t)h;
            out.color_format = GECND_PIX_FMT_RGBA8888;
            dst = NULL;
            break;
        }

        const int yw = w & ~1, yh = h & ~1;
        if (yw <= 0 || yh <= 0) {
            fprintf(stderr, "[libwebp] too small after even-align %dx%d\n", w, h);
            break;
        }
        const int    cw     = yw / 2, ch = yh / 2;
        const size_t y_size = (size_t)yw * yh;
        const size_t c_size = (size_t)cw * ch;

        dst = malloc(y_size + 2 * c_size);
        if (!dst) { fprintf(stderr, "[libwebp] oom dst %zu\n", y_size + 2 * c_size); break; }

        if (yw != w || yh != h) {
            config.options.use_cropping = 1;
            config.options.crop_left    = 0;
            config.options.crop_top     = 0;
            config.options.crop_width   = yw;
            config.options.crop_height  = yh;
        }

        config.output.colorspace      = MODE_YUV;
        config.output.u.YUVA.y        = dst;
        config.output.u.YUVA.u        = dst + y_size;
        config.output.u.YUVA.v        = dst + y_size + c_size;
        config.output.u.YUVA.y_stride = yw;
        config.output.u.YUVA.u_stride = cw;
        config.output.u.YUVA.v_stride = cw;
        config.output.u.YUVA.y_size   = y_size;
        config.output.u.YUVA.u_size   = c_size;
        config.output.u.YUVA.v_size   = c_size;

        st = WebPDecode(data, len, &config);
        if (st != VP8_STATUS_OK) {
            fprintf(stderr, "[libwebp] decode (yuv420 %dx%d): status=%d\n", w, h, st);
            break;
        }

        out.pixels       = dst;
        out.len          = y_size + 2 * c_size;
        out.w            = (int16_t)yw;
        out.h            = (int16_t)yh;
        out.color_format = GECND_PIX_FMT_YUV420P;
        dst = NULL;
    } while (0);

    WebPFreeDecBuffer(&config.output);
    free(dst);
    return out;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_decoder_async:webp:rgba8888", driver_decoder_libwebp, NULL);
    gecnd_registry("set", "image_decoder_async:webp:yuv420",   driver_decoder_libwebp, NULL);
}
