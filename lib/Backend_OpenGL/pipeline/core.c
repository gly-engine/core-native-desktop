#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include "gecnd.h"
#include "gefilter.h"
#include "geopengl.h"

void ge_pipeline_resize(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;
    glViewport(0, 0, w, h);
    mat4_ortho(s->projection, 0, (float)w, (float)h, 0, -1.0f, 1.0f);
}

static void create_atlas_page(GLBackendState *s, int width, int height) {
    GEAtlasPage page = {0};
    glGenTextures(1, &page.tex_id);
    glBindTexture(GL_TEXTURE_2D, page.tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    kv_push(GEAtlasPage, s->atlas_pages, page);
}

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    kv_init(s->atlas_pages);
    create_atlas_page(s, GE_ATLAS_SIZE, GE_ATLAS_SIZE);
    create_atlas_page(s, GE_ATLAS_SIZE, GE_ATLAS_SIZE);

    s->atlas_dirty = false;

    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, GE_MAX_VERTICES * sizeof(GEComplexVertex), NULL, GL_STREAM_DRAW);

    float post_verts[] = {
        0, 0, 0, 0,
        0, 1, 0, 1,
        1, 1, 1, 1,
        0, 0, 0, 0,
        1, 1, 1, 1,
        1, 0, 1, 0
    };
    glGenBuffers(1, &s->vbo_post);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_post);
    glBufferData(GL_ARRAY_BUFFER, sizeof(post_verts), post_verts, GL_STATIC_DRAW);

    s->fbo_id = 0;
    s->fbo_tex = 0;
    s->fbo_width = 0;
    s->fbo_height = 0;

    GEAtlasPage *p0 = &s->atlas_pages.a[0];
    p0->cursor_x = 0;
    p0->cursor_y = 1024;
    p0->row_height = 0;

    uint32_t white = 0xFFFFFFFF;
    int wx = p0->cursor_x++;
    int wy = p0->cursor_y;
    if (p0->row_height < 1) p0->row_height = 1;
    glBindTexture(GL_TEXTURE_2D, p0->tex_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, wx, wy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    s->white_uv[0] = ((float)wx + 0.5f) / (float)GE_ATLAS_SIZE;
    s->white_uv[1] = ((float)wy + 0.5f) / (float)GE_ATLAS_SIZE;

    int cx = p0->cursor_x; int cy = p0->cursor_y;
    p0->cursor_x += 64;
    if (p0->row_height < 64) p0->row_height = 64;

    unsigned char *pixels = malloc(64 * 64 * 4);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float dx = (float)x + 0.5f;
            float dy = (float)y + 0.5f;
            float dist = sqrtf((64.0f - dx)*(64.0f - dx) + (64.0f - dy)*(64.0f - dy));
            uint8_t alpha = 255;
            if (dist > 64.0f) alpha = 0;
            else if (dist > 63.0f) alpha = (uint8_t)((64.0f - dist) * 255.0f);
            int idx = (y * 64 + x) * 4;
            pixels[idx] = 255; pixels[idx+1] = 255; pixels[idx+2] = 255; pixels[idx+3] = alpha;
        }
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, cx, cy, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);

    s->corner_uv[0] = ((float)cx + 0.5f) / (float)GE_ATLAS_SIZE;
    s->corner_uv[1] = ((float)cy + 0.5f) / (float)GE_ATLAS_SIZE;
    s->corner_uv[2] = ((float)(cx + 63) + 0.5f) / (float)GE_ATLAS_SIZE;
    s->corner_uv[3] = ((float)(cy + 63) + 0.5f) / (float)GE_ATLAS_SIZE;
    s->corner_page_index = 0;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    s->batch.raw    = malloc(GE_MAX_VERTICES * sizeof(GEComplexVertex));
    s->batch.count  = 0;
    s->batch.page_index = -1;
    s->active_prog  = -1;
    s->active_page  = -1;
}

void ge_atlas_alloc(int w, int h, int *page_index, int *ox, int *oy) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        if (p->cursor_x + w > GE_ATLAS_SIZE) {
            p->cursor_x = 0; p->cursor_y += p->row_height; p->row_height = 0;
        }
        if (p->cursor_y + h <= GE_ATLAS_SIZE) {
            *page_index = i;
            *ox = p->cursor_x; *oy = p->cursor_y;
            p->cursor_x += w;
            if (h > p->row_height) p->row_height = h;
            return;
        }
    }
    int next_page = (int)kv_size(s->atlas_pages);
    create_atlas_page(s, GE_ATLAS_SIZE, GE_ATLAS_SIZE);
    GEAtlasPage *p = &s->atlas_pages.a[next_page];
    *page_index = next_page;
    *ox = p->cursor_x; *oy = p->cursor_y;
    p->cursor_x += w;
    p->row_height = h;
}

static void ensure_fbo(GLBackendState *s) {
    if (s->fbo_width == s->window_width && s->fbo_height == s->window_height && s->fbo_id != 0) return;

    if (s->fbo_id) glDeleteFramebuffers(1, &s->fbo_id);
    if (s->fbo_tex) glDeleteTextures(1, &s->fbo_tex);

    s->fbo_width = s->window_width;
    s->fbo_height = s->window_height;

    glGenTextures(1, &s->fbo_tex);
    glBindTexture(GL_TEXTURE_2D, s->fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->fbo_width, s->fbo_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &s->fbo_id);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->fbo_tex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO Incomplete\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ge_pipeline_start(void) {
    GLBackendState *s = geogl_get_state();

    glFinish();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glViewport(0, 0, s->window_width, s->window_height);
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int i = 0; i < GE_PROG_COUNT; i++) {
        glUseProgram(s->programs[i].id);
        glUniformMatrix4fv(s->programs[i].loc_proj, 1, GL_FALSE, s->projection);
    }

    s->batch.count  = 0;
    s->active_prog  = -1;
    s->active_page  = -1;
}

void ge_pipeline_terminate(void) {
    GLBackendState *s = geogl_get_state();
    glDeleteBuffers(1, &s->vbo);
    if (s->video_tex[0]) glDeleteTextures(3, s->video_tex);
    glDeleteBuffers(1, &s->vbo_post);

    if (s->fbo_id) glDeleteFramebuffers(1, &s->fbo_id);
    if (s->fbo_tex) glDeleteTextures(1, &s->fbo_tex);

    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        glDeleteTextures(1, &s->atlas_pages.a[i].tex_id);
    }
    kv_destroy(s->atlas_pages);

    free(s->batch.raw);
    kv_destroy(s->textures);
}

static void flush_batch(void) {
    GLBackendState *s = geogl_get_state();
    if (s->batch.count == 0) return;

    GEProgram *p = &s->programs[s->active_prog];
    glUseProgram(p->id);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);

    size_t stride;
    if (s->active_prog == GE_PROG_TEXTURE) {
        stride = sizeof(GETexVertex);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_SHORT,          GL_FALSE, stride, (void*)offsetof(GETexVertex, x));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE,  GL_TRUE,  stride, (void*)offsetof(GETexVertex, r));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE,  stride, (void*)offsetof(GETexVertex, u));
        glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->atlas_pages.a[s->batch.page_index].tex_id);
        glUniform1i(p->loc_tex, 0);
    } else {
        stride = sizeof(GEComplexVertex);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_SHORT,         GL_FALSE, stride, (void*)offsetof(GEComplexVertex, x));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,  stride, (void*)offsetof(GEComplexVertex, r));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_SHORT,         GL_FALSE, stride, (void*)offsetof(GEComplexVertex, px));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_SHORT,         GL_FALSE, stride, (void*)offsetof(GEComplexVertex, hw));
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_SHORT,         GL_FALSE, stride, (void*)offsetof(GEComplexVertex, radius));
        glDisableVertexAttribArray(5);
    }

    glBufferData(GL_ARRAY_BUFFER, GE_MAX_VERTICES * sizeof(GEComplexVertex), NULL, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, s->batch.count * stride, s->batch.raw);

    int offset = 0;
    while (offset < s->batch.count) {
        int chunk = (s->batch.count - offset > GE_MAX_CHUNK) ? GE_MAX_CHUNK : (s->batch.count - offset);
        glDrawArrays(GL_TRIANGLES, offset, chunk);
        offset += chunk;
    }

    s->batch.count = 0;
}

void ge_pipeline_flush_primitives(void) {
    GLBackendState *s = geogl_get_state();
    if (s->active_prog >= 0) flush_batch();
    s->active_prog = -1;
    s->active_page = -1;
}

void ge_pipeline_end(void) {
    ge_pipeline_flush_primitives();
}

void ge_pipeline_flush(void) {
    ge_pipeline_flush_primitives();
}
