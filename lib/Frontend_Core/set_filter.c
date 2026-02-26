#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gecnd.h"
#include "gefilter.h"

static gecnd_filter_t instance;

gecnd_filter_t *gecnd_filter_get_config() {
    return &instance;
}

static void project_v(gecnd_t *gly, float *x, float *y) {
    if (!gly) return;
    *x = (*x * 2.0f / (float)gly->window_width) - 1.0f;
    *y = (*y * -2.0f / (float)gly->window_height) + 1.0f;
}

static void update_video_vertices(gecnd_filter_t *filter, float x, float y, float w, float h) {
    filter->video_pos_raw.x = x;
    filter->video_pos_raw.y = y;
    filter->video_size_raw.x = w;
    filter->video_size_raw.y = h;

    gecnd_t *gly = gecnd_get_root();
    float x1 = x, y1 = y;         // TL
    float x2 = x + w, y2 = y;     // TR
    float x3 = x + w, y3 = y + h; // BR
    float x4 = x, y4 = y + h;     // BL

    project_v(gly, &x1, &y1);
    project_v(gly, &x2, &y2);
    project_v(gly, &x3, &y3);
    project_v(gly, &x4, &y4);

    // 2 Triangles (6 vertices) forming a quad: (TL, TR, BR) and (TL, BR, BL)
    float v[] = {
        x1, y1, 0.0f, 0.0f, // TL
        x2, y2, 1.0f, 0.0f, // TR
        x3, y3, 1.0f, 1.0f, // BR
        x1, y1, 0.0f, 0.0f, // TL
        x3, y3, 1.0f, 1.0f, // BR
        x4, y4, 0.0f, 1.0f  // BL
    };
    memcpy(filter->video_vertices, v, sizeof(v));
    filter->video_dirty = true;
}

static void update_corner_vertices(gecnd_filter_t *filter, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4) {
    filter->corners_raw[0].x = x1; filter->corners_raw[0].y = y1;
    filter->corners_raw[1].x = x2; filter->corners_raw[1].y = y2;
    filter->corners_raw[2].x = x3; filter->corners_raw[2].y = y3;
    filter->corners_raw[3].x = x4; filter->corners_raw[3].y = y4;

    gecnd_t *gly = gecnd_get_root();
    project_v(gly, &x1, &y1);
    project_v(gly, &x2, &y2);
    project_v(gly, &x3, &y3);
    project_v(gly, &x4, &y4);

    float v[] = {
        x1, y1, 0.0f, 0.0f,
        x2, y2, 1.0f, 0.0f,
        x3, y3, 1.0f, 1.0f,
        x1, y1, 0.0f, 0.0f,
        x3, y3, 1.0f, 1.0f,
        x4, y4, 0.0f, 1.0f
    };
    memcpy(filter->corner_vertices, v, sizeof(v));
    filter->post_dirty = true;
}

void gecnd_filter_set_brightness(float v) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->brightness = v;
    filter->video_dirty = true;
}

void gecnd_filter_set_contrast(float v) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->contrast = v;
    filter->video_dirty = true;
}

void gecnd_filter_set_saturation(float v) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->saturation = v;
    filter->video_dirty = true;
}

void gecnd_filter_set_film_grain(float v) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->film_grain = v;
    filter->video_dirty = true;
}

void gecnd_filter_set_crt(float v) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->crt_amount = v;
    filter->post_dirty = true;
}

void gecnd_filter_set_scratch(float v) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->scratch_amount = v;
    filter->video_dirty = true;
}

void gecnd_filter_set_jitter(float v) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->jitter_amount = v;
    filter->video_dirty = true;
}

void gecnd_filter_set_video_pos(float x, float y, float w, float h) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    update_video_vertices(filter, x, y, w, h);
}

void gecnd_filter_set_rotation(float angle) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->rotation_rad = angle * 3.14159265f / 180.0f;
    filter->post_dirty = true;
}

void gecnd_filter_set_aa(float blur, float wC, float wN) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    filter->aa_blur = blur;
    filter->aa_weight_center = wC;
    filter->aa_weight_neighbor = wN;
    filter->post_dirty = true;
}

void gecnd_filter_set_corners(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4) {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    update_corner_vertices(filter, x1, y1, x2, y2, x3, y3, x4, y4);
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
    filter->rotation_rad = 0.0f;
    filter->crt_amount = 0.0f;
    filter->scratch_amount = 0.0f;
    filter->jitter_amount = 0.0f;
    filter->video_dirty = true;
    filter->post_dirty = true;
}

void gecnd_filter_reset_corners() {
    gecnd_t *gly = gecnd_get_root();
    if (gly) {
        float width = (float) gly->window_width;
        float height = (float) gly->window_height;
        gecnd_filter_set_corners(0, 0, width, 0, width, height, 0, height);
    }
}

void gecnd_filter_reset_video_pos() {
    gecnd_t *gly = gecnd_get_root();
    if (gly) {
        float width = (float) gly->window_width;
        float height = (float) gly->window_height;
        gecnd_filter_set_video_pos(0, 0, width, height);
    }
}

bool gencd_filter_is_zero_corners() {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    for (int i = 0; i < 16; i++) if (filter->corner_vertices[i] != 0.0f) return false;
    return true;
}

bool gencd_filter_is_zero_video_pos() {
    gecnd_filter_t *filter = gecnd_filter_get_config();
    for (int i = 0; i < 16; i++) if (filter->video_vertices[i] != 0.0f) return false;
    return true;
}
