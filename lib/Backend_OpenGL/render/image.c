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
    GLuint tid;
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ihdr.width, ihdr.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    size_t idx = image_id - 1;
    while (kv_size(s->textures) <= idx) kv_push(GLTexture, s->textures, (GLTexture){0});
    if (kv_A(s->textures, idx).id) glDeleteTextures(1, &kv_A(s->textures, idx).id);
    kv_A(s->textures, idx) = (GLTexture){tid, (int)ihdr.width, (int)ihdr.height};
    if (success) *success = true;
    free(img); spng_ctx_free(ctx); fclose(fp);
}

void native_image_draw(int32_t image_id, int16_t x, int16_t y) {
    GLBackendState *s = geogl_get_state();
    gecnd_filter_t *filter = gecnd_filter_get_config();
    size_t idx = image_id - 1;
    if (image_id <= 0 || kv_size(s->textures) <= idx) return;
    GLTexture t = kv_A(s->textures, idx);
    if (!t.id) return;
    glUseProgram(s->texture_program);
    glUniformMatrix4fv(s->texture_loc_proj, 1, GL_FALSE, s->projection);
    glUniform1i(s->texture_loc_sampler, 0);
    glUniform2f(s->texture_loc_tsize, 1.0f / (float)t.width, 1.0f / (float)t.height);
    glUniform1f(s->texture_loc_aa_blur, filter->aa_blur);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.id);
    float v[] = {(float)x, (float)y, 0, 0, (float)x+t.width, (float)y, 1, 0, (float)x+t.width, (float)y+t.height, 1, 1, (float)x, (float)y+t.height, 0, 1};
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glEnableVertexAttribArray(s->texture_loc_pos);
    glVertexAttribPointer(s->texture_loc_pos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(s->texture_loc_texCoord);
    glVertexAttribPointer(s->texture_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableVertexAttribArray(s->texture_loc_pos);
    glDisableVertexAttribArray(s->texture_loc_texCoord);
}

void native_image_mensure(int32_t image_id, int16_t *w, int16_t *h) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    if (image_id > 0 && kv_size(s->textures) > idx) {
        GLTexture t = kv_A(s->textures, idx);
        if (t.id && w && h) { *w = (int16_t)t.width; *h = (int16_t)t.height; }
    }
}

void native_image_unload(int32_t image_id, bool *success) {
    GLBackendState *s = geogl_get_state();
    size_t idx = image_id - 1;
    if (image_id > 0 && kv_size(s->textures) > idx) {
        GLTexture *t = &kv_A(s->textures, idx);
        if (t->id) { glDeleteTextures(1, &t->id); t->id = 0; if (success) *success = true; return; }
    }
    if (success) *success = false;
}
