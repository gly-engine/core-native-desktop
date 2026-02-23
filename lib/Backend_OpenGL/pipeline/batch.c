#include "geopengl.h"

void ge_batch_add_vertex_tex(int16_t x, int16_t y,
    int16_t u, int16_t v,
    uint32_t color,
    bool opaque) {
    
    GLBackendState *s = geogl_get_state();
    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_ATLAS] : &s->transparent_batches[GE_PROG_ATLAS];
    
    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_ATLAS] : &s->transparent_batches[GE_PROG_ATLAS];
    }

    GEAtlasVertex *vertex = &((GEAtlasVertex*)b->buffer)[b->count++];
    
    vertex->x = (float)x;
    vertex->y = (float)y;
    vertex->z = (float)s->current_z;
    vertex->u0 = (float)u / 32767.0f;
    vertex->v0 = (float)v / 32767.0f;
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
    
    GEProgramType type = (mode == 0) ? GE_PROG_SIMPLE : GE_PROG_COMPLEX;
    GEBatch *b = opaque ? &s->opaque_batches[type] : &s->transparent_batches[type];

    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[type] : &s->transparent_batches[type];
    }

    if (type == GE_PROG_SIMPLE) {
        GESimpleShapeVertex *vertex = &((GESimpleShapeVertex*)b->buffer)[b->count++];
        vertex->x = (float)x;
        vertex->y = (float)y;
        vertex->z = (float)s->current_z;
        uint8_t *c = (uint8_t*)&color;
        vertex->r = c[0]; vertex->g = c[1]; vertex->b = c[2]; vertex->a = c[3];
    } else {
        GEDShapeComplexVertex *vertex = &((GEDShapeComplexVertex*)b->buffer)[b->count++];
        vertex->x = (float)x;
        vertex->y = (float)y;
        vertex->z = (float)s->current_z;
        vertex->lx = (float)lx / 32767.0f;
        vertex->ly = (float)ly / 32767.0f;
        vertex->radius = (float)radius / 32767.0f;
        vertex->mode = (float)(mode < 0 ? -mode : mode);
        uint8_t *c = (uint8_t*)&color;
        vertex->r = c[0]; vertex->g = c[1]; vertex->b = c[2]; vertex->a = c[3];
    }
}
