#include "backend_gl_internal.h"
#include <math.h>

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

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    float vertices[8];
    vertices[0] = (float)x;
    vertices[1] = (float)y;
    vertices[2] = (float)x + w;
    vertices[3] = (float)y;
    vertices[4] = (float)x + w;
    vertices[5] = (float)y + h;
    vertices[6] = (float)x;
    vertices[7] = (float)y + h;

    glUseProgram(g_shape_program);

    float projection[16];
    mat4_ortho(projection, 0, g_window_width, g_window_height, 0, -1, 1);
    glUniformMatrix4fv(g_shape_loc_proj, 1, GL_FALSE, projection);

    float color[4] = {
        ((g_current_color >> 24) & 0xFF) / 255.0f,
        ((g_current_color >> 16) & 0xFF) / 255.0f,
        ((g_current_color >> 8) & 0xFF) / 255.0f,
        (g_current_color & 0xFF) / 255.0f,
    };
    glUniform4fv(g_shape_loc_color, 1, color);

    float rect_uniform[4] = {(float)x, (float)y, (float)w, (float)h};
    glUniform4fv(g_shape_loc_rect, 1, rect_uniform);
    glUniform1f(g_shape_loc_radius, (float)r);
    glUniform1i(g_shape_loc_mode, mode);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(g_shape_loc_pos);
    glVertexAttribPointer(g_shape_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(g_shape_loc_pos);
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    float vertices[4];
    vertices[0] = (float)x1;
    vertices[1] = (float)y1;
    vertices[2] = (float)x2;
    vertices[3] = (float)y2;

    glUseProgram(g_line_program);

    float projection[16];
    mat4_ortho(projection, 0, g_window_width, g_window_height, 0, -1, 1);
    glUniformMatrix4fv(g_line_loc_proj, 1, GL_FALSE, projection);

    float color[4] = {
        ((g_current_color >> 24) & 0xFF) / 255.0f,
        ((g_current_color >> 16) & 0xFF) / 255.0f,
        ((g_current_color >> 8) & 0xFF) / 255.0f,
        (g_current_color & 0xFF) / 255.0f,
    };
    glUniform4fv(g_line_loc_color, 1, color);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(g_line_loc_pos);
    glVertexAttribPointer(g_line_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glDrawArrays(GL_LINES, 0, 2);

    glDisableVertexAttribArray(g_line_loc_pos);
}
