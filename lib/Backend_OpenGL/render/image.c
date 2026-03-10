#include <spng.h>

#include "gefilter.h"
#include "gehook.h"
#include "geopengl.h"

void native_image_load(const char *path, int32_t image_id, bool *success) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    bool reuse = false;
    int ox, oy, page_idx = 0;
    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) { if(success) *success = false; return; }
    FILE *fp = fopen(path, "rb");
    if (!fp) { spng_ctx_free(ctx); if(success) *success = false; return; }
    spng_set_png_file(ctx, fp);
    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr)) { spng_ctx_free(ctx); fclose(fp); if(success) *success = false; return; }
    if (kv_size(s->textures) > idx) {
        GLTexture old = kv_A(s->textures, idx);
        if (old.width == (int)ihdr.width && old.height == (int)ihdr.height) {
            reuse = true; 
            ox = (int)(old.u * (float)GE_ATLAS_SIZE); 
            oy = (int)(old.v * (float)GE_ATLAS_SIZE);
            page_idx = old.page_index;
        }
    }
    size_t sz;
    spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &sz);
    unsigned char *img = malloc(sz);
    spng_decode_image(ctx, img, sz, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS | SPNG_DECODE_GAMMA);

    bool is_opaque = true;
    for (size_t i = 0; i < sz; i += 4) {
        if (img[i+3] < 254) {
            is_opaque = false;
            break;
        }
    }

    if (!reuse) ge_atlas_alloc(ihdr.width, ihdr.height, &page_idx, &ox, &oy);
    ge_pipeline_flush_primitives();
    glBindTexture(GL_TEXTURE_2D, s->atlas_pages.a[page_idx].tex_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, ihdr.width, ihdr.height, GL_RGBA, GL_UNSIGNED_BYTE, img);
    s->atlas_dirty = true;
    while (kv_size(s->textures) <= idx) kv_push(GLTexture, s->textures, (GLTexture){0});
    GLTexture *t = &kv_A(s->textures, idx);
    t->id = s->atlas_pages.a[page_idx].tex_id; t->width = ihdr.width; t->height = ihdr.height;
    t->u = (float)ox / (float)GE_ATLAS_SIZE; t->v = (float)oy / (float)GE_ATLAS_SIZE;
    t->u2 = (float)(ox + ihdr.width) / (float)GE_ATLAS_SIZE; t->v2 = (float)(oy + ihdr.height) / (float)GE_ATLAS_SIZE;
    t->is_opaque = is_opaque;
    t->page_index = page_idx;
    if (success) *success = true;
    free(img); spng_ctx_free(ctx); fclose(fp);
}

void native_image_draw(int32_t image_id, int16_t x, int16_t y) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    if (image_id <= 0 || kv_size(s->textures) <= idx) return;
    GLTexture t = kv_A(s->textures, idx);
    if (!t.width) return;
    static const uint32_t color = 0xFFFFFFFF;
    int16_t ix = x, iy = y, iw = (int16_t)t.width, ih = (int16_t)t.height;
    
    ge_batch_add_vertex_tex(ix, iy, t.u, t.v, color, t.is_opaque, t.page_index);
    ge_batch_add_vertex_tex(ix, iy + ih, t.u, t.v2, color, t.is_opaque, t.page_index);
    ge_batch_add_vertex_tex(ix + iw, iy + ih, t.u2, t.v2, color, t.is_opaque, t.page_index);
    ge_batch_add_vertex_tex(ix, iy, t.u, t.v, color, t.is_opaque, t.page_index);
    ge_batch_add_vertex_tex(ix + iw, iy + ih, t.u2, t.v2, color, t.is_opaque, t.page_index);
    ge_batch_add_vertex_tex(ix + iw, iy, t.u2, t.v, color, t.is_opaque, t.page_index);

    s->current_z++;
}

void native_image_mensure(int32_t image_id, int16_t *w, int16_t *h) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    if (image_id > 0 && kv_size(s->textures) > idx) {
        GLTexture t = kv_A(s->textures, idx);
        if (t.width && w && h) { *w = (int16_t)t.width; *h = (int16_t)t.height; }
    }
}

void native_image_unload(int32_t image_id, bool *success) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    if (image_id > 0 && kv_size(s->textures) > idx) {
        kv_A(s->textures, idx).width = 0;
        if (success) *success = true; return;
    }
    if (success) *success = false;
}
