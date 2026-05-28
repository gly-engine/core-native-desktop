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
    
    vertex->x = (int16_t)x;
    vertex->y = (int16_t)y;
    vertex->z = ge_zindex_get(GE_PROG_ATLAS, opaque, page_index);
    vertex->w = 1;
    vertex->u = (uint16_t)(u * 65535.0f);
    vertex->v = (uint16_t)(v * 65535.0f);

    uint8_t *c = (uint8_t*)&color;
    vertex->r = c[0];
    vertex->g = c[1];
    vertex->b = c[2];
    vertex->a = c[3];
}

void ge_batch_add_vertex_yuv(int16_t x, int16_t y,
    float u, float v,
    uint32_t color,
    int page_index) {

    GLBackendState *s = geogl_get_state();
    /* YUV images carry no alpha — always opaque. page_index selects the
     * yuv atlas triplet (Y/Cb/Cr) the flush will bind. */
    GEBatch *b = &s->opaque_batches[GE_PROG_ATLAS_YUV];

    if (b->count >= GE_MAX_VERTICES || (b->count > 0 && b->page_index != page_index)) {
        ge_pipeline_flush_primitives();
        b = &s->opaque_batches[GE_PROG_ATLAS_YUV];
    }

    b->page_index = page_index;

    GEAtlasVertex *vertex = &((GEAtlasVertex*)b->buffer)[b->count++];

    vertex->x = (int16_t)x;
    vertex->y = (int16_t)y;
    vertex->z = ge_zindex_get(GE_PROG_ATLAS_YUV, false, page_index);
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
    
    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_SIMPLE] : &s->transparent_batches[GE_PROG_SIMPLE];

    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_SIMPLE] : &s->transparent_batches[GE_PROG_SIMPLE];
    }

    GESimpleShapeVertex *vertex = &((GESimpleShapeVertex*)b->buffer)[b->count++];
    vertex->x = (int16_t)x;
    vertex->y = (int16_t)y;
    vertex->z = ge_zindex_get(GE_PROG_SIMPLE, opaque, -1);
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
    float mode)
{
    GLBackendState *s = geogl_get_state();
    uint8_t alpha = (color >> 24) & 0xFF;
    bool opaque = false;
    
    GEBatch *b = opaque ? &s->opaque_batches[GE_PROG_COMPLEX] : &s->transparent_batches[GE_PROG_COMPLEX];

    if (b->count >= GE_MAX_VERTICES) {
        ge_pipeline_flush_primitives();
        b = opaque ? &s->opaque_batches[GE_PROG_COMPLEX] : &s->transparent_batches[GE_PROG_COMPLEX];
    }

    GEDShapeComplexVertex *vertex = &((GEDShapeComplexVertex*)b->buffer)[b->count++];
    vertex->x = (int16_t)x;
    vertex->y = (int16_t)y;
    vertex->z = ge_zindex_get(GE_PROG_COMPLEX, false, -1);
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

