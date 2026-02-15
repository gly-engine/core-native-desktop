#include "gehook.h"
#include "geopengl.h"
#include "gemedia.h"

static void draw_video_background(void) {
    GLBackendState *state = geogl_get_state();
    MediaFrame *frame = avlib_get_background_frame();

    if (!frame || !frame->pixels) {
        if (state->video_texture_id != 0) {
            glDeleteTextures(1, &state->video_texture_id);
            state->video_texture_id = 0;
            state->video_width = 0;
            state->video_height = 0;
        }
        glClearColor(state->clear_color[0], state->clear_color[1], state->clear_color[2], state->clear_color[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    if (state->video_texture_id == 0 || state->video_width != frame->width || state->video_height != frame->height) {
        state->video_width = frame->width;
        state->video_height = frame->height;
        if (state->video_texture_id == 0) {
            glGenTextures(1, &state->video_texture_id);
        }
        glBindTexture(GL_TEXTURE_2D, state->video_texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame->width, frame->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->pixels);
    } else {
        glBindTexture(GL_TEXTURE_2D, state->video_texture_id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RGBA, GL_UNSIGNED_BYTE, frame->pixels);
    }

    glUseProgram(state->texture_program);
    glUniformMatrix4fv(state->texture_loc_proj, 1, GL_FALSE, state->projection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state->video_texture_id);
    glUniform1i(state->texture_loc_sampler, 0);

    float w = (float)state->window_width;
    float h = (float)state->window_height;
    float vertices[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        w,    0.0f, 1.0f, 0.0f,
        w,    h,    1.0f, 1.0f,
        0.0f, h,    0.0f, 1.0f
    };

    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

    glEnableVertexAttribArray(state->texture_loc_pos);
    glVertexAttribPointer(state->texture_loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(state->texture_loc_texCoord);
    glVertexAttribPointer(state->texture_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(state->texture_loc_pos);
    glDisableVertexAttribArray(state->texture_loc_texCoord);
    
    glClear(GL_DEPTH_BUFFER_BIT);
}


void native_draw_start(void) {
    draw_video_background();
}

void native_draw_flush(void) {
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
    
    float vertices[8];
    vertices[0] = (float)x;
    vertices[1] = (float)y;
    vertices[2] = (float)x + w;
    vertices[3] = (float)y;
    vertices[4] = (float)x + w;
    vertices[5] = (float)y + h;
    vertices[6] = (float)x;
    vertices[7] = (float)y + h;

    glUseProgram(state->shape_program);

    glUniformMatrix4fv(state->shape_loc_proj, 1, GL_FALSE, state->projection);
    glUniform4fv(state->shape_loc_color, 1, state->current_color);

    float rect_uniform[4] = {(float)x, (float)y, (float)w, (float)h};
    glUniform4fv(state->shape_loc_rect, 1, rect_uniform);
    glUniform1f(state->shape_loc_radius, (float)r);
    glUniform1i(state->shape_loc_mode, mode);

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

    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

    glEnableVertexAttribArray(state->line_loc_pos);
    glVertexAttribPointer(state->line_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glDrawArrays(GL_LINES, 0, 2);

    glDisableVertexAttribArray(state->line_loc_pos);
}
