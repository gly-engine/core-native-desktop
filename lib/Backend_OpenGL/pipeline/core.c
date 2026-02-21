#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gecnd.h"
#include "gefilter.h"
#include "geopengl.h"

#define MAX_VERTICES ((512 * 1024) / sizeof(GEDrawVertex))
#define MAX_COMMANDS 1024

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();

    s->window_width = w;
    s->window_height = h;
    
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

    if (!s->white_texture) {
        glGenTextures(1, &s->white_texture);
        glBindTexture(GL_TEXTURE_2D, s->white_texture);
        uint32_t white = 0xFFFFFFFF;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    if (!s->batch_buffer) s->batch_buffer = malloc(512 * 1024);
    if (!s->commands) s->commands = malloc(sizeof(BatchCommand) * MAX_COMMANDS);
    s->batch_count = 0;
    s->command_count = 0;
    s->command_capacity = MAX_COMMANDS;

    gecnd_filter_t *filter = gecnd_filter_get_config();
    float quad[] = { -1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1 };
    memcpy(filter->corner_vertices, quad, sizeof(quad));
    float v_quad[] = { -1,-1,0,1, 1,-1,1,1, 1,1,1,0, -1,1,0,0 };
    memcpy(filter->video_vertices, v_quad, sizeof(v_quad));
    filter->post_dirty = filter->video_dirty = true;
}

void ge_batch_start_command(GLuint tex) {
    GLBackendState *s = geogl_get_state();
    if (s->command_count > 0 && s->commands[s->command_count - 1].texture == tex) return;
    if (s->command_count >= s->command_capacity) {
        printf("[WARN] Max batch commands reached\n");
        return;
    }
    s->commands[s->command_count].texture = tex;
    s->commands[s->command_count].start = s->batch_count;
    s->commands[s->command_count].count = 0;
    s->command_count++;
}

void ge_pipeline_flush_primitives(void) {
    GLBackendState *s = geogl_get_state();
    if (s->batch_count == 0 || s->command_count == 0) return;

    glUseProgram(s->draw_program);
    glUniformMatrix4fv(s->draw_loc_proj, 1, GL_FALSE, s->projection);
    glUniform1i(s->draw_loc_sampler, 0);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, s->batch_count * sizeof(GEDrawVertex), s->batch_buffer, GL_STREAM_DRAW);

    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3); glEnableVertexAttribArray(4);

    size_t stride = sizeof(GEDrawVertex);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(4 * sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float) + 4 * sizeof(uint8_t)));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float) + 4 * sizeof(uint8_t)));

    for (int i = 0; i < s->command_count; i++) {
        glBindTexture(GL_TEXTURE_2D, s->commands[i].texture);
        glDrawArrays(GL_TRIANGLES, s->commands[i].start, s->commands[i].count);
    }

    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);

    s->batch_count = 0;
    s->command_count = 0;
}
