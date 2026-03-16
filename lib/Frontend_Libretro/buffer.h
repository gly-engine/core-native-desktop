#ifndef GECND_LIBRETRO_BUFFER_H
#define GECND_LIBRETRO_BUFFER_H

#include <stdint.h>

// Copy Libretro RETRO_PIXEL_FORMAT_XRGB8888 (memory: BGRX) -> RGBA8888
// dst_stride and src_stride are in bytes
void libretro_copy_xrgb8888(uint8_t *dst, int dst_stride,
                             const uint8_t *src, int src_stride,
                             int w, int h);

// Copy Libretro RGB565 row-by-row (handles pitch != linesize)
// dst_stride and src_stride are in bytes
void libretro_copy_rgb565(uint8_t *dst, int dst_stride,
                          const uint8_t *src, int src_stride,
                          int w, int h);

#endif
