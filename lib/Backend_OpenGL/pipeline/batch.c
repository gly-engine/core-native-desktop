#include "geopengl.h"

void ge_batch_add_vertex_tex(int16_t x, int16_t y,
    float u, float v,
    uint32_t color,
    bool opaque,
    int page_index)
{
    (void)opaque;
    GLBackendState *s = geogl_get_state();

    if (s->active_prog != GE_PROG_TEXTURE || s->active_page != page_index || s->batch.count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
    }

    s->active_prog      = GE_PROG_TEXTURE;
    s->active_page      = page_index;
    s->batch.page_index = page_index;

    GETexVertex *vert = &s->batch.tex[s->batch.count++];
    vert->x = x;
    vert->y = y;
    uint8_t *c = (uint8_t*)&color;
    vert->r = c[0]; vert->g = c[1]; vert->b = c[2]; vert->a = c[3];
    vert->u = (uint16_t)(u * 65535.0f + 0.5f);
    vert->v = (uint16_t)(v * 65535.0f + 0.5f);
}

void ge_batch_add_vertex_complex(
    float x, float y,
    float px, float py,
    float hw, float hh,
    float radius,
    uint32_t color,
    bool opaque)
{
    (void)opaque;
    GLBackendState *s = geogl_get_state();

    if (s->active_prog != GE_PROG_COMPLEX || s->batch.count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
    }

    s->active_prog = GE_PROG_COMPLEX;

    GEComplexVertex *vert = &s->batch.complex[s->batch.count++];
    vert->x      = (int16_t)x;
    vert->y      = (int16_t)y;
    vert->px     = (int16_t)px;
    vert->py     = (int16_t)py;
    vert->hw     = (int16_t)hw;
    vert->hh     = (int16_t)hh;
    vert->radius = (int16_t)radius;
    vert->_pad   = 0;
    uint8_t *c = (uint8_t*)&color;
    vert->r = c[0]; vert->g = c[1]; vert->b = c[2]; vert->a = c[3];
}
