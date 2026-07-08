#define STB_IMAGE_IMPLEMENTATION                                                                                                                                                
#include <stb_image.h>

#include "gecnd.h"

static gamely_img_decoded_t driver_decoder_stb(const uint8_t *data, size_t len) {                                                                                                    
    gamely_img_decoded_t out = {0};                                                                                                                                             
    int w, h, ch;                                                                                                                                                               
    uint8_t *pixels = stbi_load_from_memory(data, (int)len, &w, &h, &ch, 4);
    if (!pixels) return out;
    out.pixels = pixels;
    out.len = (size_t)w * (size_t)h * 4;
    out.w = (int16_t)w;
    out.h = (int16_t)h;
    return out;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_decoder_async:bmp:rgba8888",  driver_decoder_stb, NULL);
    gecnd_registry("set", "image_decoder_async:gif:rgba8888",  driver_decoder_stb, NULL);
    gecnd_registry("set", "image_decoder_async:png:rgba8888",  driver_decoder_stb, NULL);
}
