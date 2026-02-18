#include "gehook.h"
#include "geopengl.h"
#include "gecnd.h"

static void ensure_fbo(void) {
    GLBackendState *state = geogl_get_state();
    gecnd_t *gly = gecnd_get_root();
    
    if (gly->filter_aa[0] <= 0.0f) {
        if (state->aa_fbo != 0) {
            glDeleteFramebuffers(1, &state->aa_fbo);
            glDeleteTextures(1, &state->aa_fbo_texture);
            state->aa_fbo = 0;
            state->aa_fbo_texture = 0;
        }
        return;
    }

    int target_w = state->window_width;
    int target_h = state->window_height;

    if (state->aa_fbo == 0) {
        state->aa_fbo_width = target_w;
        state->aa_fbo_height = target_h;

        glGenTextures(1, &state->aa_fbo_texture);
        glBindTexture(GL_TEXTURE_2D, state->aa_fbo_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target_w, target_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &state->aa_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, state->aa_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state->aa_fbo_texture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "[ERROR] FBO incomplete\n");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

static void draw_video_background(void) {
    native_draw_background_video();
}


void native_draw_start(void) {
    GLBackendState *state = geogl_get_state();
    gecnd_t *gly = gecnd_get_root();

    ensure_fbo();
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, state->window_width, state->window_height);
    mat4_ortho(state->projection, 0, state->window_width, state->window_height, 0, -1, 1);
    
    draw_video_background();

    if (gly->filter_aa[0] > 0.0f) {
        glBindFramebuffer(GL_FRAMEBUFFER, state->aa_fbo);
        glViewport(0, 0, state->aa_fbo_width, state->aa_fbo_height);
        mat4_ortho(state->projection, 0, state->window_width, state->window_height, 0, -1, 1);
        
        // Accumulate alpha correctly in the FBO
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void native_draw_flush(void) {
    GLBackendState *state = geogl_get_state();
    gecnd_t *gly = gecnd_get_root();

    if (gly->filter_aa[0] > 0.0f) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, state->window_width, state->window_height);
        mat4_ortho(state->projection, 0, state->window_width, state->window_height, 0, -1, 1);

        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(state->aa_program);
        glUniformMatrix4fv(state->aa_loc_proj, 1, GL_FALSE, state->projection);
        glUniform1i(state->aa_loc_sampler, 0);
        glUniform1f(state->aa_loc_blur, gly->filter_aa[0]);
        glUniform1f(state->aa_loc_weightCenter, gly->filter_aa[1]);
        glUniform1f(state->aa_loc_weightNeighbor, gly->filter_aa[2]);
        glUniform2f(state->aa_loc_texelSize, 1.0f / (float)state->aa_fbo_width, 1.0f / (float)state->aa_fbo_height);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state->aa_fbo_texture);

        float w = (float)state->window_width;
        float h = (float)state->window_height;
        float vertices[] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            w,    0.0f, 1.0f, 1.0f,
            w,    h,    1.0f, 0.0f,
            0.0f, h,    0.0f, 0.0f
        };

        glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

        glEnableVertexAttribArray(state->aa_loc_pos);
        glVertexAttribPointer(state->aa_loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(state->aa_loc_texCoord);
        glVertexAttribPointer(state->aa_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableVertexAttribArray(state->aa_loc_pos);
        glDisableVertexAttribArray(state->aa_loc_texCoord);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    platform_swap_buffers();
}

void native_draw_color(uint32_t color) {
    set_color_from_u32(geogl_get_state()->current_color, color);
}

void native_draw_clear(uint32_t color) {
    set_color_from_u32(geogl_get_state()->clear_color, color);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *state = geogl_get_state();
    
    float p = 0.0f; 
    float vertices[8];
    vertices[0] = (float)x - p;
    vertices[1] = (float)y - p;
    vertices[2] = (float)x + w + p;
    vertices[3] = (float)y - p;
    vertices[4] = (float)x + w + p;
    vertices[5] = (float)y + h + p;
    vertices[6] = (float)x - p;
    vertices[7] = (float)y + h + p;

    glUseProgram(state->shape_program);

    glUniformMatrix4fv(state->shape_loc_proj, 1, GL_FALSE, state->projection);
    glUniform4fv(state->shape_loc_color, 1, state->current_color);

    // Cap radius to half of the smallest dimension to ensure a perfect circle when r is large
    float max_r = (w < h ? (float)w : (float)h) / 2.0f;
    float final_r = (float)r > max_r ? max_r : (float)r;

    float rect_uniform[4] = {(float)x, (float)y, (float)w, (float)h};
    glUniform4fv(state->shape_loc_rect, 1, rect_uniform);
    glUniform1f(state->shape_loc_radius, final_r);
    glUniform1i(state->shape_loc_mode, mode);
    glUniform1f(state->shape_loc_thickness, GE_LINE_WIDTH);

    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

    glEnableVertexAttribArray(state->shape_loc_pos);
    glVertexAttribPointer(state->shape_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(state->shape_loc_pos);
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    GLBackendState *state = geogl_get_state();

    float vertices[4];
    vertices[0] = (float)x1;
    vertices[1] = (float)y1;
    vertices[2] = (float)x2;
    vertices[3] = (float)y2;

    glUseProgram(state->line_program);

    glUniformMatrix4fv(state->line_loc_proj, 1, GL_FALSE, state->projection);
    glUniform4fv(state->line_loc_color, 1, state->current_color);

    glLineWidth(GE_LINE_WIDTH);

    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

    glEnableVertexAttribArray(state->line_loc_pos);
    glVertexAttribPointer(state->line_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glDrawArrays(GL_LINES, 0, 2);

    glDisableVertexAttribArray(state->line_loc_pos);
}
