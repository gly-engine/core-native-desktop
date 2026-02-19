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

void gecnd_filter_set_crt(float v) {
    gecnd_filter_get_config()->crt_amount = v;
}

void gecnd_filter_set_scratch(float v) {
    gecnd_filter_get_config()->scratch_amount = v;
}

void gecnd_filter_set_jitter(float v) {
    gecnd_filter_get_config()->jitter_amount = v;
}

void gecnd_filter_set_video_pos(float x, float y, float w, float h) {
    gecnd_filter_t *f = gecnd_filter_get_config();
    f->video_pos.x = x;
    f->video_pos.y = y;
    f->video_size.x = w;
    f->video_size.y = h;
}

void gecnd_filter_set_rotation(float angle) {
    gecnd_filter_get_config()->rotation = angle;
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

void gecnd_filter_reset_effects() {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->brightness = 1.0f;
    filter->contrast = 1.0f;
    filter->saturation = 1.0f;
    filter->film_grain = 0.0f;
    filter->aa_blur = 0.0f;
    filter->aa_weight_center = 1.0f;
    filter->aa_weight_neighbor = 0.0f;
    filter->rotation = 0.0f;
    filter->crt_amount = 0.0f;
    filter->scratch_amount = 0.0f;
    filter->jitter_amount = 0.0f;
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

void gecnd_filter_reset_video_pos() {
    gecnd_t *gly = gecnd_get_root();
    gecnd_filter_t *filter = gecnd_filter_get_config();
    if (gly) {
        float w = (float) gly->width;
        float h = (float) gly->height;
        filter->video_pos.x = 0;
        filter->video_pos.y = 0;
        filter->video_size.x = w;
        filter->video_size.y = h;
    }
}

bool gencd_filter_is_zero_corners() {
    gecnd_vec2 *c = gecnd_filter_get_config()->corners;
    return c[0].x == 0 && c[0].y == 0 && c[1].x == 0 && c[1].y == 0 && c[2].x == 0 && c[2].y == 0 && c[3].x == 0 && c[3].y == 0;
}

bool gencd_filter_is_zero_video_pos() {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    return filter->video_pos.x == 0 && filter->video_pos.y == 0 && filter->video_size.x == 0 && filter->video_size.y == 0;
}
