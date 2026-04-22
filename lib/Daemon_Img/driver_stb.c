#define STB_IMAGE_IMPLEMENTATION                                                                                                                                                
#include <stb_image.h>

#include "gecnd.h"

gamely_img_decoded_t gamely_driver_decoder_stb(const uint8_t *data, size_t len) {                                                                                                    
    gamely_img_decoded_t out = {0};                                                                                                                                             
    int w, h, ch;                                                                                                                                                               
    uint8_t *pixels = stbi_load_from_memory(data, (int)len, &w, &h, &ch, 4);
    if (!pixels) return out;                                                                                                                                                    
    out.pixels = pixels;                                                                                                                                                        
    out.w = (int16_t)w;                                                                                                                                                         
    out.h = (int16_t)h;                                                                                                                                                         
    return out; 
}
