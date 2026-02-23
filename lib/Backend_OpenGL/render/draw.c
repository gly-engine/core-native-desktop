#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gecnd.h"
#include "gefilter.h"
#include "gehook.h"
#include "geopengl.h"

void ge_batch_get_color_u8(uint8_t *c) {
    GLBackendState *s = geogl_get_state();
    memcpy(c, s->current_color.rgba, 4);
}

void native_draw_start(void) {
    ge_pipeline_start();
}

void native_draw_flush() {
    ge_pipeline_end();
}

void native_draw_finish(void) {
    ge_pipeline_end();
    platform_swap_buffers();
}

void native_draw_color(uint32_t color)
{
    geogl_get_state()->current_color.u32 = __builtin_bswap32(color);
}

void native_draw_clear(uint32_t color) {
    GLBackendState *s = geogl_get_state();
    native_draw_color(color);
    native_draw_rect(0, 0, 0, s->window_width, s->window_height, 0);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *s = geogl_get_state();
    uint32_t color = s->current_color.u32;
    
    int16_t min_dim = (w < h) ? w : h;
    int16_t norm_r = 0;
    if (min_dim > 0 && r > 0) {
        float fr = (float)r / (min_dim * 0.5f);
        if (fr > 1.0f) fr = 1.0f;
        norm_r = (int16_t)(fr * 32767.0f);
    }

    // mode 0: fill, 1: frame (border)
    int8_t internal_mode = (mode == 1) ? 2 : 0;

    if (internal_mode == 2 || norm_r > 0) {
        GEProgram *p = &s->programs[GE_PROG_COMPLEX];
        glUseProgram(p->id);
        glUniform2f(p->loc_size, (float)w, (float)h);
    }

    ge_batch_add_vertex_shape(x, y, -32767, -32767, norm_r, color, internal_mode, false);
    ge_batch_add_vertex_shape(x, y + h, -32767, 32767, norm_r, color, internal_mode, false);
    ge_batch_add_vertex_shape(x + w, y + h, 32767, 32767, norm_r, color, internal_mode, false);

    ge_batch_add_vertex_shape(x, y, -32767, -32767, norm_r, color, internal_mode, false);
    ge_batch_add_vertex_shape(x + w, y + h, 32767, 32767, norm_r, color, internal_mode, false);
    ge_batch_add_vertex_shape(x + w, y, 32767, -32767, norm_r, color, internal_mode, false);

    s->current_z++;
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    GLBackendState *s = geogl_get_state();
    uint32_t color = s->current_color.u32;
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float w = GE_LINE_WIDTH / 2.0f;
    float px[4] = {(float)x1 + nx * w, (float)x2 + nx * w, (float)x2 - nx * w, (float)x1 - nx * w};
    float py[4] = {(float)y1 + ny * w, (float)y2 + ny * w, (float)y2 - ny * w, (float)y1 - ny * w};
    
    ge_batch_add_vertex_shape((int16_t)px[0], (int16_t)py[0], -32767, -32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[1], (int16_t)py[1], -32767, 32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[2], (int16_t)py[2], 32767, 32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[0], (int16_t)py[0], -32767, -32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[2], (int16_t)py[2], 32767, 32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[3], (int16_t)py[3], 32767, -32767, 0, color, 0, false);

    s->current_z++;
}
