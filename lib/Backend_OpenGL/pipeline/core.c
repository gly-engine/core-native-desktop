#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include "gecnd.h"
#include "gefilter.h"
#include "geopengl.h"


static uint8_t *g_readpixels_buf     = NULL;
static int      g_readpixels_buf_len = 0;

#define MAX_VERTICES GE_MAX_VERTICES

void ge_pipeline_resize(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;
    glViewport(0, 0, w, h);
    mat4_ortho(s->projection, 0, (float)w, (float)h, 0, -(float)GE_MAX_LAYERS, (float)GE_MAX_LAYERS);

    int needed = w * h * 4;
    if (needed > g_readpixels_buf_len) {
        free(g_readpixels_buf);
        g_readpixels_buf     = malloc((size_t)needed);
        g_readpixels_buf_len = needed;
    }
}

static void init_batch(GEBatch *b, size_t stride) {
    b->buffer = malloc(MAX_VERTICES * stride);
    b->count = 0;
    b->page_index = -1;
}

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;

    int needed = w * h * 4;
    if (needed > g_readpixels_buf_len) {
        free(g_readpixels_buf);
        g_readpixels_buf     = malloc((size_t)needed);
        g_readpixels_buf_len = needed;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    ge_atlas_init();
    s->etc1_supported = ge_detect_etc1_support();
    // Page 0: Font Atlas (Top Half 1024x1024) + System Glyphs/Images (Bottom Half 1024x2048)
    ge_atlas_create_page(GECND_PIX_FMT_RGBA8888, true);
    // Page 1: General Atlas
    ge_atlas_create_page(GECND_PIX_FMT_RGBA8888, true);

    s->atlas_dirty = false;
    
    // VBOs
    glGenBuffers(1, &s->vbo_simple);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_simple);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * sizeof(GESimpleShapeVertex), NULL, GL_STREAM_DRAW);

    glGenBuffers(1, &s->vbo_complex);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_complex);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * sizeof(GEDShapeComplexVertex), NULL, GL_STREAM_DRAW);

    glGenBuffers(1, &s->vbo_atlas);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_atlas);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * sizeof(GEAtlasVertex), NULL, GL_STREAM_DRAW);

    // Post-processing VBO (Full screen quad)
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
    
    // Page 0 setup
    GEAtlasPage *p0 = &s->atlas_pages.a[0];
    p0->cursor_x = 0;
    p0->cursor_y = 1024; // Reservado: Metade de cima para Fontes, Baixo para imagens
    p0->row_height = 0;

    // White pixel
    uint32_t white = 0xFFFFFFFF;
    int wx = p0->cursor_x++;
    int wy = p0->cursor_y;
    if (p0->row_height < 1) p0->row_height = 1;
    glBindTexture(GL_TEXTURE_2D, p0->tex_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, wx, wy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    s->white_uv[0] = ((float)wx + 0.5f) / (float)GE_ATLAS_SIZE;
    s->white_uv[1] = ((float)wy + 0.5f) / (float)GE_ATLAS_SIZE;

    // Shared UI alpha8 page: Daemon_Font glyphs (top-left 1024x1024) + this corner mask,
    // so text and rounded-rect corners batch under GE_PROG_ALPHA8 with one texture.
    // Reserved from the shelf allocator (laid out by hand + Daemon_Font/driver_freetype.c).
    int cp = ge_atlas_create_page(GECND_PIX_FMT_ALPHA8, false);
    GEAtlasPage *up = &s->atlas_pages.a[cp];
    up->cursor_x = up->reset_cursor_x = GE_ATLAS_SIZE;
    up->cursor_y = up->reset_cursor_y = GE_ATLAS_SIZE;
    int cx = GE_FONT_ATLAS_SIZE, cy = 0;  /* corner sits past the font glyph atlas region */
    unsigned char *pixels = malloc(64 * 64);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float dx = (float)x + 0.5f;
            float dy = (float)y + 0.5f;
            float dist = sqrtf((64.0f - dx)*(64.0f - dx) + (64.0f - dy)*(64.0f - dy));
            uint8_t alpha = 255;
            if (dist > 64.0f) alpha = 0;
            else if (dist > 63.0f) alpha = (uint8_t)((64.0f - dist) * 255.0f);
            pixels[y * 64 + x] = alpha;
        }
    }
    glBindTexture(GL_TEXTURE_2D, up->tex_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, cx, cy, 64, 64, GL_ALPHA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    free(pixels);

    s->corner_uv[0] = (float)cx / (float)GE_ATLAS_SIZE;
    s->corner_uv[1] = (float)cy / (float)GE_ATLAS_SIZE;
    s->corner_uv[2] = (float)(cx + 64) / (float)GE_ATLAS_SIZE;
    s->corner_uv[3] = (float)(cy + 64) / (float)GE_ATLAS_SIZE;
    s->corner_page_index = cp;

    /* Snapshot post-init cursors so ge_atlas_reset_images() can restore them */
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        p->reset_cursor_x   = p->cursor_x;
        p->reset_cursor_y   = p->cursor_y;
        p->reset_row_height = p->row_height;
    }

    // Batches
    init_batch(&s->opaque_batches[GE_PROG_SIMPLE], sizeof(GESimpleShapeVertex));
    init_batch(&s->opaque_batches[GE_PROG_COMPLEX], sizeof(GEDShapeComplexVertex));
    init_batch(&s->opaque_batches[GE_PROG_ATLAS], sizeof(GEAtlasVertex));
    init_batch(&s->opaque_batches[GE_PROG_ATLAS_YUV], sizeof(GEAtlasVertex));
    init_batch(&s->transparent_batches[GE_PROG_SIMPLE], sizeof(GESimpleShapeVertex));
    init_batch(&s->transparent_batches[GE_PROG_COMPLEX], sizeof(GEDShapeComplexVertex));
    init_batch(&s->transparent_batches[GE_PROG_ATLAS], sizeof(GEAtlasVertex));
    init_batch(&s->transparent_batches[GE_PROG_ALPHA8], sizeof(GEAtlasVertex));
}

bool ge_detect_etc1_support(void) {
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    return ext && strstr(ext, "GL_OES_compressed_ETC1_RGB8_texture") != NULL;
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
    // ensure_fbo(s);
    // glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_id);

    glViewport(0, 0, s->window_width, s->window_height);
    glClearColor(0, 0, 0, 0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0, -(float)GE_MAX_LAYERS, (float)GE_MAX_LAYERS);
    
    for(int i=0; i<GE_PROG_COUNT; i++) {
        s->opaque_batches[i].count = 0;
        s->transparent_batches[i].count = 0;
    }
    s->active_opaque_page_index = -1;
    s->active_transparent_page_index = -1;
    ge_zindex_reset();
}

void ge_pipeline_terminate(void) {
    free(g_readpixels_buf);
    g_readpixels_buf     = NULL;
    g_readpixels_buf_len = 0;
    GLBackendState *s = geogl_get_state();
    glDeleteBuffers(1, &s->vbo_simple);
    glDeleteBuffers(1, &s->vbo_complex);
    glDeleteBuffers(1, &s->vbo_atlas);
    if (s->video_tex[0]) glDeleteTextures(3, s->video_tex);
    glDeleteBuffers(1, &s->vbo_post);

    if (s->fbo_id)  glDeleteFramebuffers(1, &s->fbo_id);
    if (s->fbo_tex) glDeleteTextures(1,    &s->fbo_tex);

    // HW render FBO (cores like PCSX ReARMed)
    if (s->hw_fbo_id)       glDeleteFramebuffers(1,  &s->hw_fbo_id);
    if (s->hw_fbo_tex)      glDeleteTextures(1,       &s->hw_fbo_tex);
    if (s->hw_fbo_depth_rb) glDeleteRenderbuffers(1,  &s->hw_fbo_depth_rb);
    
    ge_atlas_terminate();

    for(int i=0; i<GE_PROG_COUNT; i++) {
        free(s->opaque_batches[i].buffer);
        free(s->transparent_batches[i].buffer);
    }
    kv_destroy(s->textures);
}

static void flush_batch(GEProgramType type, bool transparent) {
    GLBackendState *s = geogl_get_state();
    GEBatch *b = transparent ? &s->transparent_batches[type] : &s->opaque_batches[type];
    if (b->count == 0) return;

    GEProgram *p = &s->programs[type];
    glUseProgram(p->id);
    glUniformMatrix4fv(p->loc_proj, 1, GL_FALSE, s->projection);

    if (transparent) {
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    GLuint vbo = 0;
    size_t stride = 0;
    if (type == GE_PROG_SIMPLE) {
        vbo = s->vbo_simple;
        stride = sizeof(GESimpleShapeVertex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, stride, (void*)offsetof(GESimpleShapeVertex, x));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GESimpleShapeVertex, r));
        glDisableVertexAttribArray(2); glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
    } else if (type == GE_PROG_COMPLEX) {
        vbo = s->vbo_complex;
        stride = sizeof(GEDShapeComplexVertex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, stride, (void*)offsetof(GEDShapeComplexVertex, x));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_SHORT, GL_FALSE, stride, (void*)offsetof(GEDShapeComplexVertex, px));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_SHORT, GL_FALSE, stride, (void*)offsetof(GEDShapeComplexVertex, hw));
        glEnableVertexAttribArray(5); glVertexAttribPointer(5, 1, GL_SHORT, GL_FALSE, stride, (void*)offsetof(GEDShapeComplexVertex, radius));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GEDShapeComplexVertex, r));
        glDisableVertexAttribArray(4); // Mode removed
        
        glDisableVertexAttribArray(6);
    } else if (type == GE_PROG_ATLAS || type == GE_PROG_ALPHA8) {
        vbo = s->vbo_atlas;
        stride = sizeof(GEAtlasVertex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, stride, (void*)offsetof(GEAtlasVertex, x));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GEAtlasVertex, r));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, (void*)offsetof(GEAtlasVertex, u));
        glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);

        glActiveTexture(GL_TEXTURE0);
        if (b->page_index & GECND_ATLAS_ETC_PAGE_FLAG) {
            /* Standalone ETC1 texture id encoded directly in page_index. */
            GLuint tex = (GLuint)(b->page_index & ~GECND_ATLAS_ETC_PAGE_FLAG);
            glBindTexture(GL_TEXTURE_2D, tex);
        } else if (b->page_index >= 0 && b->page_index < (int)kv_size(s->atlas_pages)) {
            /* Single-texture atlas page (rgba8888 / rgba5551). */
            glBindTexture(GL_TEXTURE_2D, s->atlas_pages.a[b->page_index].tex_id);
        }
        glUniform1i(p->loc_tex, 0);
    } else if (type == GE_PROG_ATLAS_YUV) {
        vbo = s->vbo_atlas;
        stride = sizeof(GEAtlasVertex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, stride, (void*)offsetof(GEAtlasVertex, x));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GEAtlasVertex, r));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, (void*)offsetof(GEAtlasVertex, u));
        glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);

        if (b->page_index >= 0 && b->page_index < (int)kv_size(s->atlas_pages)) {
            GEAtlasPage *yp = &s->atlas_pages.a[b->page_index];
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, yp->tex_y); glUniform1i(p->loc_tex_y, 0);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, yp->tex_u); glUniform1i(p->loc_tex_u, 1);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, yp->tex_v); glUniform1i(p->loc_tex_v, 2);
            glActiveTexture(GL_TEXTURE0);
        }
    }

    // Orphan the buffer to avoid driver stalls/crashes
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * stride, NULL, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, b->count * stride, b->buffer);
    
    int offset = 0;
    while (offset < b->count) {
        int chunk = (b->count - offset > GE_MAX_CHUNK) ? GE_MAX_CHUNK : (b->count - offset);
        glDrawArrays(GL_TRIANGLES, offset, chunk);
        offset += chunk;
    }

    b->count = 0;
}

void ge_pipeline_flush_primitives(void) {
    // Flush Opaque first
    flush_batch(GE_PROG_SIMPLE, false);
    flush_batch(GE_PROG_ATLAS, false);
    flush_batch(GE_PROG_ATLAS_YUV, false);  // YUV images are always opaque

    // Flush Transparent
    flush_batch(GE_PROG_SIMPLE, true);
    flush_batch(GE_PROG_ATLAS, true);
    flush_batch(GE_PROG_ALPHA8, true);
    flush_batch(GE_PROG_COMPLEX, true);
}

void ge_pipeline_flush(void) {
    ge_pipeline_flush_primitives();
    bool online = gamely_daemon_media_transmit_is_online();

    static bool prev_online  = false;
    static int  push_count   = 0;

    if (online && g_readpixels_buf) {
        GLBackendState *s = geogl_get_state();
        glReadPixels(0, 0, s->window_width, s->window_height,
                     GL_RGBA, GL_UNSIGNED_BYTE, g_readpixels_buf);
        push_count++;
        if (push_count <= 5 || push_count % 30 == 0)
            printf("[pipeline] push #%d  %dx%d\n",
                   push_count, s->window_width, s->window_height);
        gamely_daemon_media_transmit_push(g_readpixels_buf,
                                          s->window_width, s->window_height);
    }
}
