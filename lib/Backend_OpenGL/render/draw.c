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

void ge_batch_add_vertex(float x, float y, float u, float v, uint8_t *color, float *rect, float *params) {
    GLBackendState *s = geogl_get_state();
    if (s->batch_count >= MAX_VERTICES) return;
    
    GEDrawVertex *vertex = &s->batch_buffer[s->batch_count++];
    vertex->x = x; vertex->y = y;
    vertex->u = u; vertex->v = v;
    memcpy(vertex->color, color, 4);
    if (rect) memcpy(vertex->rect, rect, 4 * sizeof(float));
    else memset(vertex->rect, 0, 4 * sizeof(float));
    if (params) memcpy(vertex->params, params, 4 * sizeof(float));
    else memset(vertex->params, 0, 4 * sizeof(float));

    if (s->command_count > 0) {
        s->commands[s->command_count - 1].count++;
    }
}

void native_draw_start(void) {
    ge_pipeline_start();
}

void native_draw_finish(void) {
    ge_pipeline_flush_primitives(); // Ensure UI is flushed before swap
    platform_swap_buffers();
}

void native_draw_color(uint32_t color) {
    set_color_from_u32(geogl_get_state()->current_color, color);
}

void native_draw_clear(uint32_t color) {
    set_color_from_u32(geogl_get_state()->clear_color, color);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *s = geogl_get_state();
    (void)mode; (void)r;

    if (s->batch_count + 6 >= MAX_VERTICES) {
        static bool warned = false;
        if (!warned) { printf("[WARN] Vertex buffer full\n"); warned = true; }
        return;
    }

    ge_batch_start_command(s->white_texture);

    uint8_t color[4];
    ge_batch_get_color_u8(color);

    float fx = (float)x; float fy = (float)y;
    float fw = (float)w; float fh = (float)h;

    ge_batch_add_vertex(fx, fy, 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(fx, fy+fh, 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(fx+fw, fy+fh, 0, 0, color, NULL, NULL);

    ge_batch_add_vertex(fx, fy, 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(fx+fw, fy+fh, 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(fx+fw, fy, 0, 0, color, NULL, NULL);
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    GLBackendState *s = geogl_get_state();
    
    if (s->batch_count + 6 >= MAX_VERTICES) return;

    ge_batch_start_command(s->white_texture);

    uint8_t color[4];
    ge_batch_get_color_u8(color);

    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    
    float nx = -dy / len;
    float ny = dx / len;
    float w = GE_LINE_WIDTH / 2.0f;

    float px[4] = {(float)x1 + nx * w, (float)x2 + nx * w, (float)x2 - nx * w, (float)x1 - nx * w};
    float py[4] = {(float)y1 + ny * w, (float)y2 + ny * w, (float)y2 - ny * w, (float)y1 - ny * w};

    ge_batch_add_vertex(px[0], py[0], 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(px[1], py[1], 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(px[2], py[2], 0, 0, color, NULL, NULL);

    ge_batch_add_vertex(px[0], py[0], 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(px[2], py[2], 0, 0, color, NULL, NULL);
    ge_batch_add_vertex(px[3], py[3], 0, 0, color, NULL, NULL);
}
