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
    glBindTexture(GL_TEXTURE_2D, s->post_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gecnd_filter_reset_corners();
    gecnd_filter_reset_video_pos();
}

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w; s->window_height = h;
    
    // Post FBO
    if (s->post_fbo_texture) glDeleteTextures(1, &s->post_fbo_texture);
    if (s->post_fbo) glDeleteFramebuffers(1, &s->post_fbo);
    glGenTextures(1, &s->post_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, s->post_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &s->post_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->post_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->post_fbo_texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Mega Atlas Init
    if (s->atlas_id) glDeleteTextures(1, &s->atlas_id);
    glGenTextures(1, &s->atlas_id);
    glBindTexture(GL_TEXTURE_2D, s->atlas_id);
    s->atlas_width = GE_ATLAS_SIZE; s->atlas_height = GE_ATLAS_SIZE;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->atlas_width, s->atlas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Triple VBOs (GE_BATCH_SIZE each)
    if (s->vbos[0]) glDeleteBuffers(3, s->vbos);
    glGenBuffers(3, s->vbos);
    s->vbo_idx = 0;
    for(int i=0; i<3; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, s->vbos[i]);
        glBufferData(GL_ARRAY_BUFFER, GE_BATCH_SIZE, NULL, GL_STREAM_DRAW);
    }

    s->alloc_cursor_x = GE_FONT_ATLAS_SIZE; s->alloc_cursor_y = 0; s->alloc_row_height = 0;
    int wx, wy; ge_atlas_alloc(1, 1, &wx, &wy); 
    uint32_t white = 0xFFFFFFFF;
    glTexSubImage2D(GL_TEXTURE_2D, 0, wx, wy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    s->white_uv[0] = ((float)wx + 0.5f) / (float)s->atlas_width;
    s->white_uv[1] = ((float)wy + 0.5f) / (float)s->atlas_height;

    if (!s->batch_buffer) s->batch_buffer = malloc(GE_BATCH_SIZE);
    s->batch_count = 0;

    gecnd_filter_t *filter = gecnd_filter_get_config();
    float quad[] = { 0,0,0,0, (float)w,0,1,0, (float)w,(float)h,1,1, 0,(float)h,0,1 };
    memcpy(filter->video_vertices, quad, sizeof(quad));
    filter->video_dirty = true;
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Direct to screen for now
    glViewport(0, 0, s->window_width, s->window_height);
    glClearColor(s->clear_color[0], s->clear_color[1], s->clear_color[2], s->clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0, -1, 1);
    
    // Cycle VBO and Orphan whole frame buffer
    s->vbo_idx = (s->vbo_idx + 1) % 3;
    glBindBuffer(GL_ARRAY_BUFFER, s->vbos[s->vbo_idx]);
    glBufferData(GL_ARRAY_BUFFER, GE_BATCH_SIZE, NULL, GL_STREAM_DRAW);

    s->batch_count = 0;
    native_draw_background_video();
}

void ge_pipeline_flush_primitives(void) {
    GLBackendState *s = geogl_get_state();
    if (s->batch_count == 0 || !s->batch_buffer) return;

    glUseProgram(s->draw_program);
    glUniformMatrix4fv(s->draw_loc_proj, 1, GL_FALSE, s->projection);
    glUniform1i(s->draw_loc_atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->atlas_id);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbos[s->vbo_idx]);
    
    // Upload EVERYTHING in one go. Using glBufferData is safer for many GLES2 drivers.
    glBufferData(GL_ARRAY_BUFFER, s->batch_count * sizeof(GEDrawVertex), s->batch_buffer, GL_DYNAMIC_DRAW);

    size_t stride = sizeof(GEDrawVertex);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GEDrawVertex, x));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GEDrawVertex, u));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GEDrawVertex, color));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_BYTE, GL_TRUE, stride, (void*)offsetof(GEDrawVertex, local));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 2, GL_UNSIGNED_BYTE, GL_FALSE, stride, (void*)offsetof(GEDrawVertex, sdf));
    glEnableVertexAttribArray(5); glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GEDrawVertex, size));

    int offset = 0;
    while (offset < s->batch_count) {
        int chunk = (s->batch_count - offset > GE_MAX_CHUNK) ? GE_MAX_CHUNK : (s->batch_count - offset);
        glDrawArrays(GL_TRIANGLES, offset, chunk);
        offset += chunk;
    }

    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3); glDisableVertexAttribArray(4); glDisableVertexAttribArray(5);

    s->batch_count = 0;
}

void ge_pipeline_flush(void) {
    ge_pipeline_flush_primitives();
}
