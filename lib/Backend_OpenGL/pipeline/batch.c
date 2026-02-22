#include "geopengl.h"

void ge_batch_add_vertex_tex(int16_t x, int16_t y,
    int16_t u, int16_t v,
    uint32_t color,
    bool aa) {
    (void) aa;

    GLBackendState *s = geogl_get_state();
    if (s->batch_count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
    }

    GEDrawVertex *vertex = &s->batch_buffer[s->batch_count++];
    
    vertex->x = x;
    vertex->y = y;
    vertex->param.tex.u = u;
    vertex->param.tex.v = v;
    vertex->param.tex.pad1 = 1;
    vertex->param.tex.pad2 = 1;

    uint8_t *c = (uint8_t*)&color;
    vertex->r = c[0];
    vertex->g = c[1];
    vertex->b = c[2];
    vertex->a = c[3];
}

void ge_batch_add_vertex_shape(
    int16_t x, int16_t y,
    int16_t lx, int16_t ly,
    int16_t radius,
    uint32_t color,
    int8_t mode,
    bool aa)
{
    (void) aa;

    GLBackendState *s = geogl_get_state();
    if (s->batch_count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
    }

    GEDrawVertex *vertex = &s->batch_buffer[s->batch_count++];

    vertex->x = x;
    vertex->y = y;
    vertex->param.shape.mode = -mode;
    vertex->param.shape.radius = radius;
    vertex->param.shape.lx = lx;
    vertex->param.shape.ly = ly;

    uint8_t *c = (uint8_t*)&color;
    vertex->r = c[0];
    vertex->g = c[1];
    vertex->b = c[2];
    vertex->a = c[3];
}
