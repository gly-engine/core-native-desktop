#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

static gecnd_filter_t instance;

gecnd_filter_t *gecnd_filter_get_config() {
    return &instance;
}

void gecnd_filter_set_brightness(float v) {
    gecnd_filter_get_config()->brightness = v;
}

void gecnd_filter_set_contrast(float v) {
    gecnd_filter_get_config()->contrast = v;
}

void gecnd_filter_set_saturation(float v) {
    gecnd_filter_get_config()->saturation = v;
}

void gecnd_filter_set_film_grain(float v) {
    gecnd_filter_get_config()->film_grain = v;
}

void gecnd_filter_set_sharpen(float v) {
    gecnd_filter_get_config()->sharpen = v;
}

void gecnd_filter_set_aa(float blur, float wC, float wN) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->aa_blur = blur;
    filter->aa_weight_center = wC;
    filter->aa_weight_neighbor = wN;
}

void gecnd_filter_set_corners(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->corners[0].x = x1;
    filter->corners[0].y = y1;
    filter->corners[1].x = x2;
    filter->corners[1].y = y2;
    filter->corners[2].x = x3;
    filter->corners[2].y = y3;
    filter->corners[3].x = x4;
    filter->corners[3].y = y4;
}

void gecnd_filter_reset_all() {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->brightness = 1.0f;
    filter->contrast = 1.0f;
    filter->saturation = 1.0f;
    filter->film_grain = 0.0f;
    filter->sharpen = 0.0f;
    filter->aa_blur = 0.0f;
    filter->aa_weight_center = 1.0f;
    filter->corners[0].x = 0;
    filter->corners[0].y = 0;
    filter->corners[1].x = 0;
    filter->corners[1].y = 0;
    filter->corners[2].x = 0;
    filter->corners[2].y = 0;
    filter->corners[3].x = 0;
    filter->corners[3].y = 0;
}

void gecnd_filter_reset_corners() {
    gecnd_t *gly = gecnd_get_root();
    gecnd_filter_t *filter = gecnd_filter_get_config();
    if (gly) {
        float w = (float) gly->width;
        float h = (float) gly->height;
        filter->corners[0].x = 0;
        filter->corners[0].y = 0;
        filter->corners[1].x = w;
        filter->corners[1].y = 0;
        filter->corners[2].x = w;
        filter->corners[2].y = h;
        filter->corners[3].x = 0;
        filter->corners[3].y = h;
    }
}

bool gencd_filter_is_zero_corners() {
    gecnd_vec2 *c = gecnd_filter_get_config()->corners;
    return c[0].x == 0 && c[0].y == 0 && c[1].x == 0 && c[1].y == 0 && c[2].x == 0 && c[2].y == 0 && c[3].x == 0 && c[3].y == 0;
}
