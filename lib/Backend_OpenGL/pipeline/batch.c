#include "geopengl.h"

static void flush_if_other_transparent(GLBackendState *s, GEProgramType self) {
    bool need = false;
    for (int i = 0; i < GE_PROG_COUNT; i++) {
        if (i != (int)self && s->transparent_batches[i].count > 0) {
            need = true;
            break;
        }
    }
    if (need) ge_pipeline_flush_primitives();
}

void ge_batch_add_vertex_tex(int16_t x, int16_t y,
    float u, float v,
    uint32_t color,
    bool opaque,
    int page_index) {
    
    GLBackendState *s = geogl_get_state();

    if (!opaque)
        flush_if_other_transparent(s, GE_PROG_ATLAS);

    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_ATLAS] : &s->transparent_batches[GE_PROG_ATLAS];
    
    if (b->count >= GE_MAX_VERTICES || (b->count > 0 && b->page_index != page_index)) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_ATLAS] : &s->transparent_batches[GE_PROG_ATLAS];
    }
    
    b->page_index = page_index;

    GEAtlasVertex *vertex = &((GEAtlasVertex*)b->buffer)[b->count++];
    
    vertex->x = (int16_t)x;
    vertex->y = (int16_t)y;
    vertex->z = (int16_t)s->current_z;
    vertex->w = 1;
    vertex->u = (uint16_t)(u * 65535.0f);
    vertex->v = (uint16_t)(v * 65535.0f);

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

    if (!opaque)
        flush_if_other_transparent(s, GE_PROG_SIMPLE);

    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_SIMPLE] : &s->transparent_batches[GE_PROG_SIMPLE];

    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_SIMPLE] : &s->transparent_batches[GE_PROG_SIMPLE];
    }

    GESimpleShapeVertex *vertex = &((GESimpleShapeVertex*)b->buffer)[b->count++];
    vertex->x = (int16_t)x;
    vertex->y = (int16_t)y;
    vertex->z = (int16_t)s->current_z;
    vertex->w = 1;
    uint8_t *c = (uint8_t*)&color;
    vertex->r = c[0];
    vertex->g = c[1];
    vertex->b = c[2];
    vertex->a = c[3];
}

void ge_batch_add_vertex_complex(
    float x, float y,
    float px, float py,
    float hw, float hh,
    float radius,
    uint32_t color,
    float mode,
    bool fill)
{
    GLBackendState *s = geogl_get_state();
    (void)(color >> 24);

    flush_if_other_transparent(s, GE_PROG_COMPLEX);

    GEBatch *b = &s->transparent_batches[GE_PROG_COMPLEX];

    int fill_mode = fill ? 1 : 0;
    if (b->count > 0 && s->complex_batch_is_fill != fill_mode) {
        ge_pipeline_flush_primitives();
        b = &s->transparent_batches[GE_PROG_COMPLEX];
    }
    s->complex_batch_is_fill = fill_mode;

    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = &s->transparent_batches[GE_PROG_COMPLEX];
    }

    GEDShapeComplexVertex *vertex = &((GEDShapeComplexVertex*)b->buffer)[b->count++];
    vertex->x = (int16_t)x;
    vertex->y = (int16_t)y;
    vertex->z = (int16_t)s->current_z;
    vertex->w = 1;
    vertex->px = (int16_t)px;
    vertex->py = (int16_t)py;
    vertex->hw = (int16_t)hw;
    vertex->hh = (int16_t)hh;
    vertex->radius = (int16_t)radius;
    vertex->pad = 0;
    uint8_t *c = (uint8_t*)&color;
    vertex->r = c[0];
    vertex->g = c[1];
    vertex->b = c[2];
    vertex->a = c[3];
}

