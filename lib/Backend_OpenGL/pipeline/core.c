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
    mat4_ortho(s->projection, 0, (float)w, (float)h, 0, -1, 1);
}

static void init_batch(GEBatch *b, size_t stride) {
    b->buffer = malloc(MAX_VERTICES * stride);
    b->count = 0;
}

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // Mega Atlas Init
    if (s->atlas_id) glDeleteTextures(1, &s->atlas_id);
    glGenTextures(1, &s->atlas_id);
    glBindTexture(GL_TEXTURE_2D, s->atlas_id);
    s->atlas_width = GE_ATLAS_SIZE; s->atlas_height = GE_ATLAS_SIZE;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->atlas_width, s->atlas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
    
    s->alloc_cursor_x = GE_FONT_ATLAS_SIZE; s->alloc_cursor_y = 0; s->alloc_row_height = 0;
    int wx, wy; ge_atlas_alloc(1, 1, &wx, &wy); 
    uint32_t white = 0xFFFFFFFF;
    glTexSubImage2D(GL_TEXTURE_2D, 0, wx, wy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    s->white_uv[0] = ((float)wx + 0.5f) / (float)s->atlas_width;
    s->white_uv[1] = ((float)wy + 0.5f) / (float)s->atlas_height;

    // Batches
    init_batch(&s->opaque_batches[GE_PROG_SIMPLE], sizeof(GESimpleShapeVertex));
    init_batch(&s->opaque_batches[GE_PROG_COMPLEX], sizeof(GEDShapeComplexVertex));
    init_batch(&s->opaque_batches[GE_PROG_ATLAS], sizeof(GEAtlasVertex));
    init_batch(&s->transparent_batches[GE_PROG_SIMPLE], sizeof(GESimpleShapeVertex));
    init_batch(&s->transparent_batches[GE_PROG_COMPLEX], sizeof(GEDShapeComplexVertex));
    init_batch(&s->transparent_batches[GE_PROG_ATLAS], sizeof(GEAtlasVertex));
}

void ge_atlas_alloc(int w, int h, int *ox, int *oy) {
    GLBackendState *s = geogl_get_state();
    if (s->alloc_cursor_x + w > s->atlas_width) {
        s->alloc_cursor_x = 0; s->alloc_cursor_y += s->alloc_row_height; s->alloc_row_height = 0;
    }
    if (s->alloc_cursor_y < GE_FONT_ATLAS_SIZE && s->alloc_cursor_x < GE_FONT_ATLAS_SIZE) s->alloc_cursor_x = GE_FONT_ATLAS_SIZE;
    if (s->alloc_cursor_x + w > s->atlas_width) {
         s->alloc_cursor_x = 0; s->alloc_cursor_y += s->alloc_row_height; 
         if (s->alloc_cursor_y < GE_FONT_ATLAS_SIZE) s->alloc_cursor_y = GE_FONT_ATLAS_SIZE;
         s->alloc_row_height = 0;
    }
    *ox = s->alloc_cursor_x; *oy = s->alloc_cursor_y;
    s->alloc_cursor_x += w;
    if (h > s->alloc_row_height) s->alloc_row_height = h;
}

void ge_pipeline_start(void) {
    GLBackendState *s = geogl_get_state();
    glViewport(0, 0, s->window_width, s->window_height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0, -1000, 1000);
    
    for(int i=0; i<GE_PROG_COUNT; i++) {
        s->opaque_batches[i].count = 0;
        s->transparent_batches[i].count = 0;
    }
    s->current_z = 0;
}

void ge_pipeline_terminate(void) {
    GLBackendState *s = geogl_get_state();
    glDeleteBuffers(1, &s->vbo_simple);
    glDeleteBuffers(1, &s->vbo_complex);
    glDeleteBuffers(1, &s->vbo_atlas);
    if (s->atlas_id) glDeleteTextures(1, &s->atlas_id);
    if (s->video_tex[0]) glDeleteTextures(3, s->video_tex);
    
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
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0); // x,y,z
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)12); // lx, ly
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)20); // mode
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)24); // radius
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)28); // r,g,b,a
        
        glUniform2f(p->loc_size, 100.0f, 100.0f); // TODO: pass actual size
        glUniform1f(p->loc_thickness, 2.0f);
        glUniform1f(p->loc_aa_blur, 1.0f);
    } else if (type == GE_PROG_ATLAS) {
        vbo = s->vbo_atlas;
        stride = sizeof(GEAtlasVertex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0); // x,y,z
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)12); // r,g,b,a
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)16); // u0,v0,u1,v1
        glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->atlas_id);
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
    flush_batch(GE_PROG_COMPLEX, false);

    // Flush Transparent
    flush_batch(GE_PROG_SIMPLE, true);
    flush_batch(GE_PROG_ATLAS, true);
    flush_batch(GE_PROG_COMPLEX, true);
}

void ge_pipeline_end(void) {
    ge_pipeline_flush_primitives();
    glDepthMask(GL_TRUE);
}

void ge_pipeline_flush(void) {
    ge_pipeline_flush_primitives();
}
