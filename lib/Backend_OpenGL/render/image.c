#include <spng.h>

#include "gefilter.h"
#include "gehook.h"
#include "geopengl.h"

void native_image_load(const char *path, int32_t image_id, bool *success) {
    GLBackendState *s = geogl_get_state();
    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) { if(success) *success = false; return; }
    FILE *fp = fopen(path, "rb");
    if (!fp) { spng_ctx_free(ctx); if(success) *success = false; return; }
    spng_set_png_file(ctx, fp);
    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr)) { spng_ctx_free(ctx); fclose(fp); if(success) *success = false; return; }
    size_t sz;
    spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &sz);
    unsigned char *img = malloc(sz);
    spng_decode_image(ctx, img, sz, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS | SPNG_DECODE_GAMMA);
    
    // Allocate in Atlas
    int ox, oy;
    ge_atlas_alloc(ihdr.width, ihdr.height, &ox, &oy);
    
    // Upload to Atlas
    glBindTexture(GL_TEXTURE_2D, s->atlas_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, ihdr.width, ihdr.height, GL_RGBA, GL_UNSIGNED_BYTE, img);
    
    // Store UVs
    size_t idx = image_id - 1;
    while (kv_size(s->textures) <= idx) kv_push(GLTexture, s->textures, (GLTexture){0});
    GLTexture *t = &kv_A(s->textures, idx);
    
    t->id = s->atlas_id;
    t->width = ihdr.width;
    t->height = ihdr.height;
    t->u = (float)ox / s->atlas_width;
    t->v = (float)oy / s->atlas_height;
    t->u2 = (float)(ox + ihdr.width) / s->atlas_width;
    t->v2 = (float)(oy + ihdr.height) / s->atlas_height;

    if (success) *success = true;
    free(img); spng_ctx_free(ctx); fclose(fp);
}

void native_image_draw(int32_t image_id, int16_t x, int16_t y) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    if (image_id <= 0 || kv_size(s->textures) <= idx) return;
    GLTexture t = kv_A(s->textures, idx);
    if (!t.width) return;

    uint8_t color[4];
    ge_batch_get_color_u8(color);

    float fx = (float)x; float fy = (float)y;
    float fw = (float)t.width; float fh = (float)t.height;
    
    float rect[4] = {0,0,0,0}; // Disable SDF
    float data[3] = {0,0,0};

    ge_batch_add_vertex(fx, fy, t.u, t.v, color, rect, data);
    ge_batch_add_vertex(fx, fy+fh, t.u, t.v2, color, rect, data);
    ge_batch_add_vertex(fx+fw, fy+fh, t.u2, t.v2, color, rect, data);

    ge_batch_add_vertex(fx, fy, t.u, t.v, color, rect, data);
    ge_batch_add_vertex(fx+fw, fy+fh, t.u2, t.v2, color, rect, data);
    ge_batch_add_vertex(fx+fw, fy, t.u2, t.v, color, rect, data);
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
        GLTexture *t = &kv_A(s->textures, idx);
        t->width = 0;
        if (success) *success = true;
        return;
    }
    if (success) *success = false;
}
