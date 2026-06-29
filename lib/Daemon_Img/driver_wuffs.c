#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__BMP
#define WUFFS_CONFIG__MODULE__TGA
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#include "wuffs-v0.4.c"

static gamely_img_decoded_t decode_with(wuffs_base__image_decoder *dec,
                                        const uint8_t *data, size_t len) {
    gamely_img_decoded_t out    = {0};
    uint8_t             *pixels = NULL;
    uint8_t             *work   = NULL;

    do {
        wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader((uint8_t *)data, len, true);

        wuffs_base__image_config cfg;
        memset(&cfg, 0, sizeof cfg);
        wuffs_base__status st = wuffs_base__image_decoder__decode_image_config(dec, &cfg, &src);
        if (!wuffs_base__status__is_ok(&st)) break;

        uint32_t w = wuffs_base__pixel_config__width(&cfg.pixcfg);
        uint32_t h = wuffs_base__pixel_config__height(&cfg.pixcfg);
        if (!w || !h) break;

        wuffs_base__pixel_config__set(
            &cfg.pixcfg,
            WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL,
            WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, w, h);

        size_t img_size = (size_t)w * (size_t)h * 4;
        pixels = malloc(img_size);
        if (!pixels) break;

        wuffs_base__pixel_buffer pb;
        st = wuffs_base__pixel_buffer__set_from_slice(
            &pb, &cfg.pixcfg,
            wuffs_base__make_slice_u8(pixels, img_size));
        if (!wuffs_base__status__is_ok(&st)) break;

        wuffs_base__range_ii_u64 wb = wuffs_base__image_decoder__workbuf_len(dec);
        work = wb.max_incl ? malloc((size_t)wb.max_incl) : NULL;

        st = wuffs_base__image_decoder__decode_frame(
            dec, &pb, &src,
            WUFFS_BASE__PIXEL_BLEND__SRC,
            wuffs_base__make_slice_u8(work, work ? (size_t)wb.max_incl : 0),
            NULL);
        if (!wuffs_base__status__is_ok(&st)) break;

        out.pixels = pixels;
        out.len    = img_size;
        out.w      = (int16_t)w;
        out.h      = (int16_t)h;
        pixels     = NULL;
    } while (0);

    if (work) {
        free(work);
    }

    return out;
}

static gamely_img_decoded_t driver_decoder_wuffs_png(const uint8_t *data, size_t len) {
    wuffs_png__decoder dec;
    wuffs_base__status st = wuffs_png__decoder__initialize(
        &dec, sizeof dec, WUFFS_VERSION, WUFFS_INITIALIZE__DEFAULT_OPTIONS);
    if (!wuffs_base__status__is_ok(&st)) return (gamely_img_decoded_t){0};
    return decode_with(wuffs_png__decoder__upcast_as__wuffs_base__image_decoder(&dec), data, len);
}

static gamely_img_decoded_t driver_decoder_wuffs_jpeg(const uint8_t *data, size_t len) {
    wuffs_jpeg__decoder dec;
    wuffs_base__status st = wuffs_jpeg__decoder__initialize(
        &dec, sizeof dec, WUFFS_VERSION, WUFFS_INITIALIZE__DEFAULT_OPTIONS);
    if (!wuffs_base__status__is_ok(&st)) return (gamely_img_decoded_t){0};
    return decode_with(wuffs_jpeg__decoder__upcast_as__wuffs_base__image_decoder(&dec), data, len);
}

static gamely_img_decoded_t driver_decoder_wuffs_bmp(const uint8_t *data, size_t len) {
    wuffs_bmp__decoder dec;
    wuffs_base__status st = wuffs_bmp__decoder__initialize(
        &dec, sizeof dec, WUFFS_VERSION, WUFFS_INITIALIZE__DEFAULT_OPTIONS);
    if (!wuffs_base__status__is_ok(&st)) return (gamely_img_decoded_t){0};
    return decode_with(wuffs_bmp__decoder__upcast_as__wuffs_base__image_decoder(&dec), data, len);
}

static gamely_img_decoded_t driver_decoder_wuffs_tga(const uint8_t *data, size_t len) {
    wuffs_tga__decoder dec;
    wuffs_base__status st = wuffs_tga__decoder__initialize(
        &dec, sizeof dec, WUFFS_VERSION, WUFFS_INITIALIZE__DEFAULT_OPTIONS);
    if (!wuffs_base__status__is_ok(&st)) return (gamely_img_decoded_t){0};
    return decode_with(wuffs_tga__decoder__upcast_as__wuffs_base__image_decoder(&dec), data, len);
}

/**
 @todo use only wuffs png?
 */
__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_decoder_async:jpeg:rgba8888", driver_decoder_wuffs_jpeg, NULL);
    gecnd_registry("set", "image_decoder_async:jpg:rgba8888",  driver_decoder_wuffs_jpeg, NULL);
    gecnd_registry("set", "image_decoder_async:png:rgba8888",  driver_decoder_wuffs_png, NULL);
    gecnd_registry("set", "image_decoder_async:bmp:rgba8888",  driver_decoder_wuffs_bmp, NULL);
}