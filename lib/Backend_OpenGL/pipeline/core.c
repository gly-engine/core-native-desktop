#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gecnd.h"
#include "gefilter.h"
#include "geopengl.h"

#define MAX_VERTICES ((4 * 1024 * 1024) / sizeof(GEDrawVertex))

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();

    s->window_width = w;
    s->window_height = h;
    
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
    s->atlas_width = 4096;
    s->atlas_height = 4096;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->atlas_width, s->atlas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Allocator Init
    s->alloc_cursor_x = 1024;
    s->alloc_cursor_y = 0;
    s->alloc_row_height = 0;

    // White Pixel Alloc
    int wx, wy;
    ge_atlas_alloc(1, 1, &wx, &wy); 
    uint32_t white = 0xFFFFFFFF;
    glTexSubImage2D(GL_TEXTURE_2D, 0, wx, wy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    
    float u = (float)wx + 0.5f;
    float v = (float)wy + 0.5f;
    s->white_uv[0] = u / (float)s->atlas_width;
    s->white_uv[1] = v / (float)s->atlas_height;

    if (!s->batch_buffer) s->batch_buffer = malloc(512 * 1024);
    s->batch_count = 0;

    gecnd_filter_t *filter = gecnd_filter_get_config();
    float quad[] = { -1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1 };
    memcpy(filter->corner_vertices, quad, sizeof(quad));
    float v_quad[] = { -1,-1,0,1, 1,-1,1,1, 1,1,1,0, -1,1,0,0 };
    memcpy(filter->video_vertices, v_quad, sizeof(v_quad));
    filter->post_dirty = filter->video_dirty = true;
}

void ge_atlas_alloc(int w, int h, int *ox, int *oy) {
    GLBackendState *s = geogl_get_state();
    
    if (s->alloc_cursor_x + w > s->atlas_width) {
        s->alloc_cursor_x = 0;
        s->alloc_cursor_y += s->alloc_row_height;
        s->alloc_row_height = 0;
    }
    
    if (s->alloc_cursor_y < 1024 && s->alloc_cursor_x < 1024) {
        s->alloc_cursor_x = 1024;
    }
    
    if (s->alloc_cursor_x + w > s->atlas_width) {
         s->alloc_cursor_x = 0;
         s->alloc_cursor_y += s->alloc_row_height; 
         if (s->alloc_cursor_y < 1024) s->alloc_cursor_y = 1024;
         s->alloc_row_height = 0;
    }
    
    *ox = s->alloc_cursor_x;
    *oy = s->alloc_cursor_y;
    
    s->alloc_cursor_x += w;
    if (h > s->alloc_row_height) s->alloc_row_height = h;
}

void ge_pipeline_flush_primitives(void) {
    GLBackendState *s = geogl_get_state();
    if (s->batch_count == 0) return;

    glUseProgram(s->draw_program);
    glUniformMatrix4fv(s->draw_loc_proj, 1, GL_FALSE, s->projection);
    glUniform1i(s->draw_loc_atlas, 0);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, s->batch_count * sizeof(GEDrawVertex), s->batch_buffer, GL_STREAM_DRAW);

    glEnableVertexAttribArray(0); // a_pos
    glEnableVertexAttribArray(1); // a_uv
    glEnableVertexAttribArray(2); // a_color
    glEnableVertexAttribArray(3); // a_rect
    glEnableVertexAttribArray(4); // a_data

    size_t stride = sizeof(GEDrawVertex);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(4 * sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float) + 4 * sizeof(uint8_t)));
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float) + 4 * sizeof(uint8_t)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->atlas_id);
    
    glDrawArrays(GL_TRIANGLES, 0, s->batch_count);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    s->batch_count = 0;
}
