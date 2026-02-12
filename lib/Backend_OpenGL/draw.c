#include "backend_gl_internal.h"
#include <math.h>

static void set_color_from_u32(float *v, uint32_t c) {
    v[0] = ((c >> 24) & 0xFF) / 255.0f;
    v[1] = ((c >> 16) & 0xFF) / 255.0f;
    v[2] = ((c >>  8) & 0xFF) / 255.0f;
    v[3] = ( c        & 0xFF) / 255.0f;
}

static void mat4_ortho(float *mat, float left, float right, float bottom, float top, float near, float far) {
    mat[0] = 2.0f / (right - left);
    mat[1] = 0.0f;
    mat[2] = 0.0f;
    mat[3] = 0.0f;

    mat[4] = 0.0f;
    mat[5] = 2.0f / (top - bottom);
    mat[6] = 0.0f;
    mat[7] = 0.0f;

    mat[8] = 0.0f;
    mat[9] = 0.0f;
    mat[10] = -2.0f / (far - near);
    mat[11] = 0.0f;

    mat[12] = -(right + left) / (right - left);
    mat[13] = -(top + bottom) / (top - bottom);
    mat[14] = -(far + near) / (far - near);
    mat[15] = 1.0f;
}

void native_draw_start(void) {
    GLBackendState *state = geogl_get_state();
    glClearColor(state->clear_color[0], state->clear_color[1], state->clear_color[2], state->clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
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

    float projection[16];
    mat4_ortho(projection, 0, state->window_width, state->window_height, 0, -1, 1);
    glUniformMatrix4fv(state->shape_loc_proj, 1, GL_FALSE, projection);
    glUniform4fv(state->shape_loc_color, 1, state->current_color);

    float rect_uniform[4] = {(float)x, (float)y, (float)w, (float)h};
    glUniform4fv(state->shape_loc_rect, 1, rect_uniform);
    glUniform1f(state->shape_loc_radius, (float)r);
    glUniform1i(state->shape_loc_mode, mode);

    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

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

    float projection[16];
    mat4_ortho(projection, 0, state->window_width, state->window_height, 0, -1, 1);
    glUniformMatrix4fv(state->line_loc_proj, 1, GL_FALSE, projection);
    glUniform4fv(state->line_loc_color, 1, state->current_color);

    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(state->line_loc_pos);
    glVertexAttribPointer(state->line_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glDrawArrays(GL_LINES, 0, 2);

    glDisableVertexAttribArray(state->line_loc_pos);
}
