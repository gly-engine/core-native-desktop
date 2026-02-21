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

void ge_batch_add_vertex(float x, float y, float u, float v, uint32_t color, float lx, float ly, float sw, float sh, float r, float b) {
    GLBackendState *s = geogl_get_state();
    if (s->batch_count >= GE_MAX_VERTICES) ge_pipeline_flush_primitives();
    
    GEDrawVertex *vertex = &s->batch_buffer[s->batch_count++];
    vertex->x = x; vertex->y = y;
    vertex->u = u; vertex->v = v;
    vertex->color.u32 = color;

    vertex->local[0] = (int8_t)(lx * 127.0f);
    vertex->local[1] = (int8_t)(ly * 127.0f);
    vertex->sdf[0] = (uint8_t)(r > 255.0f ? 255.0f : r);
    vertex->sdf[1] = (uint8_t)(b > 255.0f ? 255.0f : b);
    vertex->size[0] = sw;
    vertex->size[1] = sh;
}

void native_draw_start(void) {
    ge_pipeline_start();
}

void native_draw_finish(void) {
    ge_pipeline_end();
    platform_swap_buffers();
}

void native_draw_color(uint32_t color)
{
    color = __builtin_bswap32(color);
    geogl_get_state()->current_color.u32 = color;
}

void native_draw_clear(uint32_t color) {
    uint8_t *v = geogl_get_state()->current_color.rgba;
    v[0] = 0xFF;
    v[1] = 0x00;
    v[2] = 0xFF;
    v[3] = 0xFF;
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *s = geogl_get_state();
    uint32_t color = s->current_color.u32;
    float fx = (float)x, fy = (float)y, fw = (float)w, fh = (float)h;
    float fr = (float)r;
    float fb = (mode == 1) ? GE_LINE_WIDTH : 0.0f;
    float u = s->white_uv[0], v = s->white_uv[1];

    ge_batch_add_vertex(fx, fy, u, v, color, -1,-1, fw, fh, fr, fb);
    ge_batch_add_vertex(fx, fy+fh, u, v, color, -1, 1, fw, fh, fr, fb);
    ge_batch_add_vertex(fx+fw, fy+fh, u, v, color, 1, 1, fw, fh, fr, fb);

    ge_batch_add_vertex(fx, fy, u, v, color, -1,-1, fw, fh, fr, fb);
    ge_batch_add_vertex(fx+fw, fy+fh, u, v, color, 1, 1, fw, fh, fr, fb);
    ge_batch_add_vertex(fx+fw, fy, u, v, color, 1, -1, fw, fh, fr, fb);
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
    float u = s->white_uv[0], v = s->white_uv[1];
    
    ge_batch_add_vertex(px[0], py[0], u, v, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(px[1], py[1], u, v, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(px[2], py[2], u, v, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(px[0], py[0], u, v, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(px[2], py[2], u, v, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(px[3], py[3], u, v, color, 0,0, 0,0, 0,0);
}
