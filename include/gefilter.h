#ifndef GEFILTER_H
#define GEFILTER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float x, y;
} gecnd_vec2;

typedef struct {
    float video_vertices[24];
    float corner_vertices[24];

    gecnd_vec2 corners_raw[4];
    gecnd_vec2 video_pos_raw;
    gecnd_vec2 video_size_raw;

    float rotation_rad;
    float crt_amount;
    float aa_blur;
    float aa_weight_center;
    float aa_weight_neighbor;

    float brightness;
    float contrast;
    float saturation;
    float film_grain;
    float scratch_amount;
    float jitter_amount;

    bool video_dirty;
    bool post_dirty;
} gecnd_filter_t;

gecnd_filter_t* gecnd_filter_get_config();

#endif
