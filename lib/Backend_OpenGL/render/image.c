#include <spng.h>

#include "gefilter.h"
#include "gehook.h"
#include "geopengl.h"

void native_image_load(const char *path, int32_t image_id, bool *success) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    bool reuse = false;
    int ox, oy;
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
            reuse = true; ox = (int)(old.u * (float)s->atlas_width); oy = (int)(old.v * (float)s->atlas_height);
        }
    }
    size_t sz;
    spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &sz);
    unsigned char *img = malloc(sz);
    spng_decode_image(ctx, img, sz, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS | SPNG_DECODE_GAMMA);
    if (!reuse) ge_atlas_alloc(ihdr.width, ihdr.height, &ox, &oy);
    glBindTexture(GL_TEXTURE_2D, s->atlas_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, ihdr.width, ihdr.height, GL_RGBA, GL_UNSIGNED_BYTE, img);
    while (kv_size(s->textures) <= idx) kv_push(GLTexture, s->textures, (GLTexture){0});
    GLTexture *t = &kv_A(s->textures, idx);
    t->id = s->atlas_id; t->width = ihdr.width; t->height = ihdr.height;
    t->u = (float)ox / (float)s->atlas_width; t->v = (float)oy / (float)s->atlas_height;
    t->u2 = (float)(ox + ihdr.width) / (float)s->atlas_width; t->v2 = (float)(oy + ihdr.height) / (float)s->atlas_height;
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
    float fx = (float)x, fy = (float)y, fw = (float)t.width, fh = (float)t.height;
    ge_batch_add_vertex(fx, fy, t.u, t.v, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(fx, fy+fh, t.u, t.v2, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(fx+fw, fy+fh, t.u2, t.v2, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(fx, fy, t.u, t.v, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(fx+fw, fy+fh, t.u2, t.v2, color, 0,0, 0,0, 0,0);
    ge_batch_add_vertex(fx+fw, fy, t.u2, t.v, color, 0,0, 0,0, 0,0);
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
