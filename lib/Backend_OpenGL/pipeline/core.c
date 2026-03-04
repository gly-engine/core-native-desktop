#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include "gecnd.h"
#include "gefilter.h"
#include "geopengl.h"

#define MAX_VERTICES GE_MAX_VERTICES

void ge_pipeline_resize(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;
    glViewport(0, 0, w, h);
    mat4_ortho(s->projection, 0, (float)w, (float)h, 0, -(float)GE_MAX_LAYERS, (float)GE_MAX_LAYERS);
}

static void init_batch(GEBatch *b, size_t stride) {
    b->buffer = malloc(MAX_VERTICES * stride);
    b->count = 0;
    b->page_index = -1;
}

static void create_atlas_page(GLBackendState *s, int width, int height) {
    GEAtlasPage page = {0};
    glGenTextures(1, &page.tex_id);
    glBindTexture(GL_TEXTURE_2D, page.tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    page.cursor_x = 0;
    page.cursor_y = 0;
    page.row_height = 0;
    kv_push(GEAtlasPage, s->atlas_pages, page);
}

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    kv_init(s->atlas_pages);
    // Page 0: Font Atlas
    create_atlas_page(s, GE_FONT_ATLAS_SIZE, GE_FONT_ATLAS_SIZE);
    // Page 1: General Atlas
    create_atlas_page(s, GE_ATLAS_SIZE, GE_ATLAS_SIZE);

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
    
    int wx, wy, wp; ge_atlas_alloc(1, 1, &wp, &wx, &wy); 
    uint32_t white = 0xFFFFFFFF;
    glBindTexture(GL_TEXTURE_2D, s->atlas_pages.a[wp].tex_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, wx, wy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    s->white_uv[0] = ((float)wx + 0.5f) / (float)GE_ATLAS_SIZE;
    s->white_uv[1] = ((float)wy + 0.5f) / (float)GE_ATLAS_SIZE;

    // Batches
    init_batch(&s->opaque_batches[GE_PROG_SIMPLE], sizeof(GESimpleShapeVertex));
    init_batch(&s->opaque_batches[GE_PROG_COMPLEX], sizeof(GEDShapeComplexVertex));
    init_batch(&s->opaque_batches[GE_PROG_ATLAS], sizeof(GEAtlasVertex));
    init_batch(&s->transparent_batches[GE_PROG_SIMPLE], sizeof(GESimpleShapeVertex));
    init_batch(&s->transparent_batches[GE_PROG_COMPLEX], sizeof(GEDShapeComplexVertex));
    init_batch(&s->transparent_batches[GE_PROG_ATLAS], sizeof(GEAtlasVertex));
}

void ge_atlas_alloc(int w, int h, int *page_index, int *ox, int *oy) {
    GLBackendState *s = geogl_get_state();
    // Search from page 1 onwards (page 0 is for fonts)
    for (int i = 1; i < (int)kv_size(s->atlas_pages); i++) {
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
    // All pages full, create a new one
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
    // ensure_fbo(s);
    // glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_id);

    glViewport(0, 0, s->window_width, s->window_height);
    glClearColor(1, 0, 1, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0, -(float)GE_MAX_LAYERS, (float)GE_MAX_LAYERS);
    
    for(int i=0; i<GE_PROG_COUNT; i++) {
        s->opaque_batches[i].count = 0;
        s->transparent_batches[i].count = 0;
    }
    s->active_opaque_page_index = -1;
    s->active_transparent_page_index = -1;
    s->current_z = 0;
}

void ge_pipeline_terminate(void) {
    GLBackendState *s = geogl_get_state();
    glDeleteBuffers(1, &s->vbo_simple);
    glDeleteBuffers(1, &s->vbo_complex);
    glDeleteBuffers(1, &s->vbo_atlas);
    if (s->video_tex[0]) glDeleteTextures(3, s->video_tex);
    glDeleteBuffers(1, &s->vbo_post);

    if (s->fbo_id) glDeleteFramebuffers(1, &s->fbo_id);
    if (s->fbo_tex) glDeleteTextures(1, &s->fbo_tex);
    
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        glDeleteTextures(1, &s->atlas_pages.a[i].tex_id);
    }
    kv_destroy(s->atlas_pages);
    
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
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0); // x,y,z
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)12); // r,g,b,a
        glDisableVertexAttribArray(2); glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
    } else if (type == GE_PROG_COMPLEX) {
        vbo = s->vbo_complex;
        stride = sizeof(GEDShapeComplexVertex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);  // a_pos
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)12); // a_local_pixel (px, py)
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)20); // a_half_size (hw, hh)
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)28); // a_mode
        glEnableVertexAttribArray(5); glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)32); // a_radius
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)36); // a_color (r,g,b,a)
        
        glDisableVertexAttribArray(6);
    } else if (type == GE_PROG_ATLAS) {
        vbo = s->vbo_atlas;
        stride = sizeof(GEAtlasVertex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0); // x,y,z
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)12); // r,g,b,a
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)16); // u0,v0,u1,v1
        glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);

        glActiveTexture(GL_TEXTURE0);
        if (b->page_index >= 0 && b->page_index < (int)kv_size(s->atlas_pages)) {
            glBindTexture(GL_TEXTURE_2D, s->atlas_pages.a[b->page_index].tex_id);
        }
        glUniform1i(p->loc_tex, 0);
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

    // Flush Transparent
    flush_batch(GE_PROG_SIMPLE, true);
    flush_batch(GE_PROG_ATLAS, true);
    flush_batch(GE_PROG_COMPLEX, true);
}

void ge_pipeline_end(void) {
    ge_pipeline_flush_primitives();
}

void ge_pipeline_flush(void) {
    ge_pipeline_flush_primitives();
}
