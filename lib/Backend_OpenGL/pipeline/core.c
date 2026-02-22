#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include "gecnd.h"
#include "gefilter.h"
#include "geopengl.h"

#define MAX_VERTICES (GE_BATCH_SIZE / sizeof(GEDrawVertex))

void ge_pipeline_resize(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;
    glViewport(0, 0, w, h);
    mat4_ortho(s->projection, 0, (float)w, (float)h, 0, -1, 1);
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
    
    // Single VBO
    if (s->vbo) glDeleteBuffers(1, &s->vbo);
    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, GE_BATCH_SIZE, NULL, GL_STREAM_DRAW);
    
    s->current_program = 0;

    s->alloc_cursor_x = GE_FONT_ATLAS_SIZE; s->alloc_cursor_y = 0; s->alloc_row_height = 0;
    int wx, wy; ge_atlas_alloc(1, 1, &wx, &wy); 
    uint32_t white = 0xFFFFFFFF;
    glTexSubImage2D(GL_TEXTURE_2D, 0, wx, wy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    s->white_uv[0] = ((float)wx + 0.5f) / (float)s->atlas_width;
    s->white_uv[1] = ((float)wy + 0.5f) / (float)s->atlas_height;

    if (!s->batch_buffer) s->batch_buffer = malloc(GE_BATCH_SIZE);
    s->batch_count = 0;
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
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0, -1, 1);
    
    s->batch_count = 0;
}

void ge_pipeline_terminate(void) {
    GLBackendState *s = geogl_get_state();
    if (s->vbo) glDeleteBuffers(1, &s->vbo);
    if (s->atlas_id) glDeleteTextures(1, &s->atlas_id);
    if (s->batch_buffer) {
        free(s->batch_buffer);
        s->batch_buffer = NULL;
    }
    kv_destroy(s->textures);
}

void ge_pipeline_flush_primitives(void) {
    GLBackendState *s = geogl_get_state();
    if (s->batch_count == 0 || !s->batch_buffer) return;

    if (s->current_program != s->draw_program) {
        glUseProgram(s->draw_program);
        glUniformMatrix4fv(s->draw_loc_proj, 1, GL_FALSE, s->projection);
        glUniform1i(s->draw_loc_tex, 0);
        
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
        size_t stride = sizeof(GEDrawVertex);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, (void*)4);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)8);
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_SHORT_4_4_4_4, GL_TRUE, stride, (void*)12);
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)14);
        glDisableVertexAttribArray(5);
        s->current_program = s->draw_program;
    }

    if (s->atlas_dirty) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->atlas_id);
        s->atlas_dirty = false;
    }

    if (s->batch_count > GE_MAX_VERTICES) s->batch_count = GE_MAX_VERTICES;

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    // Orphan the buffer to avoid driver bugs/stalls on some GLES implementations
    glBufferData(GL_ARRAY_BUFFER, GE_BATCH_SIZE, NULL, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(s->batch_count * sizeof(GEDrawVertex)), s->batch_buffer);
    
    int offset = 0;
    while (offset < s->batch_count) {
        int chunk = (s->batch_count - offset > GE_MAX_CHUNK) ? GE_MAX_CHUNK : (s->batch_count - offset);
        glDrawArrays(GL_TRIANGLES, offset, chunk);
        offset += chunk;
    }

    s->batch_count = 0;
}

void ge_pipeline_end(void) {
    ge_pipeline_flush_primitives();
}

void ge_pipeline_flush(void) {
    ge_pipeline_flush_primitives();
}
