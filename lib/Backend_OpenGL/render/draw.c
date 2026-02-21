#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gecnd.h"
#include "gefilter.h"
#include "gehook.h"
#include "geopengl.h"

#define MAX_VERTICES ((512 * 1024) / sizeof(GEDrawVertex))

void ge_batch_get_color_u8(uint8_t *c) {
    GLBackendState *s = geogl_get_state();
    c[0] = (uint8_t)(s->current_color[0] * 255.0f);
    c[1] = (uint8_t)(s->current_color[1] * 255.0f);
    c[2] = (uint8_t)(s->current_color[2] * 255.0f);
    c[3] = (uint8_t)(s->current_color[3] * 255.0f);
}

void ge_batch_add_vertex(float x, float y, float u, float v, uint8_t *color, float *rect, float *data) {
    GLBackendState *s = geogl_get_state();
    if (s->batch_count >= MAX_VERTICES) {
        ge_pipeline_flush_primitives();
    }
    
    GEDrawVertex *vertex = &s->batch_buffer[s->batch_count++];
    vertex->x = x; vertex->y = y;
    vertex->u = u; vertex->v = v;
    memcpy(vertex->color, color, 4);
    if (rect) memcpy(vertex->rect, rect, 4 * sizeof(float));
    else memset(vertex->rect, 0, 4 * sizeof(float));
    if (data) memcpy(vertex->data, data, 3 * sizeof(float));
    else memset(vertex->data, 0, 3 * sizeof(float));
}

void native_draw_start(void) {
    ge_pipeline_start();
}

void native_draw_finish(void) {
    ge_pipeline_flush_primitives();
    platform_swap_buffers();
}

void native_draw_color(uint32_t color) {
    set_color_from_u32(geogl_get_state()->current_color, 0x0000FFFF);
}

void native_draw_clear(uint32_t color) {
    set_color_from_u32(geogl_get_state()->clear_color, color);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *s = geogl_get_state();
    gecnd_filter_t *filter = gecnd_filter_get_config();

    uint8_t color[4];
    ge_batch_get_color_u8(color);

    float fx = (float)x; float fy = (float)y;
    float fw = (float)w; float fh = (float)h;
    
    float mr = (fw < fh ? fw : fh) / 2.0f;
    float fr = (float)r > mr ? mr : (float)r;
    
    // Rect for SDF: x, y, w, h
    float rect[4] = {fx, fy, fw, fh};
    // Data: radius, border, outline (0=fill, 1=outline)
    float border = (mode == 1) ? GE_LINE_WIDTH : 0.0f;
    float outline = (mode == 1) ? 1.0f : 0.0f;
    float data[3] = {fr, border, outline};
    
    // UV is white pixel
    float u = s->white_uv[0];
    float v = s->white_uv[1];

    ge_batch_add_vertex(fx, fy, u, v, color, rect, data);
    ge_batch_add_vertex(fx, fy+fh, u, v, color, rect, data);
    ge_batch_add_vertex(fx+fw, fy+fh, u, v, color, rect, data);

    ge_batch_add_vertex(fx, fy, u, v, color, rect, data);
    ge_batch_add_vertex(fx+fw, fy+fh, u, v, color, rect, data);
    ge_batch_add_vertex(fx+fw, fy, u, v, color, rect, data);
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    GLBackendState *s = geogl_get_state();
    uint8_t color[4];
    ge_batch_get_color_u8(color);

    // Lines are just solid quads rotated (handled by vertex position)
    // No SDF needed if simple line, or we can use SDF box.
    // For simplicity, render as non-SDF quad (rect.z = 0)
    
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    
    float nx = -dy / len;
    float ny = dx / len;
    float w = GE_LINE_WIDTH / 2.0f;

    float px[4] = {(float)x1 + nx * w, (float)x2 + nx * w, (float)x2 - nx * w, (float)x1 - nx * w};
    float py[4] = {(float)y1 + ny * w, (float)y2 + ny * w, (float)y2 - ny * w, (float)y1 - ny * w};

    float u = s->white_uv[0];
    float v = s->white_uv[1];
    float rect[4] = {0,0,0,0}; // Disable SDF
    float data[3] = {0,0,0};

    ge_batch_add_vertex(px[0], py[0], u, v, color, rect, data);
    ge_batch_add_vertex(px[1], py[1], u, v, color, rect, data);
    ge_batch_add_vertex(px[2], py[2], u, v, color, rect, data);

    ge_batch_add_vertex(px[0], py[0], u, v, color, rect, data);
    ge_batch_add_vertex(px[2], py[2], u, v, color, rect, data);
    ge_batch_add_vertex(px[3], py[3], u, v, color, rect, data);
}
