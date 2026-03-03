#include "geopengl.h"

void ge_batch_add_vertex_tex(int16_t x, int16_t y,
    float u, float v,
    uint32_t color,
    bool opaque,
    int page_index) {
    
    GLBackendState *s = geogl_get_state();
    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_ATLAS] : &s->transparent_batches[GE_PROG_ATLAS];
    
    if (b->count >= GE_MAX_VERTICES || (b->count > 0 && b->page_index != page_index)) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_ATLAS] : &s->transparent_batches[GE_PROG_ATLAS];
    }
    
    b->page_index = page_index;

    GEAtlasVertex *vertex = &((GEAtlasVertex*)b->buffer)[b->count++];
    
    vertex->x = (float)x;
    vertex->y = (float)y;
    vertex->z = (float)s->current_z;
    vertex->u0 = u;
    vertex->v0 = v;
    vertex->u1 = 1.0f; 
    vertex->v1 = 1.0f;

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
    uint8_t alpha = (color >> 24) & 0xFF;
    bool opaque = (alpha >= 254);
    
    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_SIMPLE] : &s->transparent_batches[GE_PROG_SIMPLE];

    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_SIMPLE] : &s->transparent_batches[GE_PROG_SIMPLE];
    }

    GESimpleShapeVertex *vertex = &((GESimpleShapeVertex*)b->buffer)[b->count++];
    vertex->x = (float)x;
    vertex->y = (float)y;
    vertex->z = (float)s->current_z;
    uint8_t *c = (uint8_t*)&color;
    vertex->r = c[0]; vertex->g = c[1]; vertex->b = c[2]; vertex->a = c[3];
}

void ge_batch_add_vertex_complex(
    float x, float y,
    float px, float py,
    float hw, float hh,
    float radius,
    uint32_t color,
    float mode)
{
    GLBackendState *s = geogl_get_state();
    uint8_t alpha = (color >> 24) & 0xFF;
    bool opaque = (alpha >= 254);
    
    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_COMPLEX] : &s->transparent_batches[GE_PROG_COMPLEX];

    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_COMPLEX] : &s->transparent_batches[GE_PROG_COMPLEX];
    }

    GEDShapeComplexVertex *vertex = &((GEDShapeComplexVertex*)b->buffer)[b->count++];
    vertex->x = x;
    vertex->y = y;
    vertex->z = (float)s->current_z;
    vertex->px = px;
    vertex->py = py;
    vertex->hw = hw;
    vertex->hh = hh;
    vertex->radius = radius;
    vertex->mode = mode;
    uint8_t *c = (uint8_t*)&color;
    vertex->r = c[0]; vertex->g = c[1]; vertex->b = c[2]; vertex->a = c[3];
}

