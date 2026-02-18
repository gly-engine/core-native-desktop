#include "gehook.h"
#include "geopengl.h"

static void draw_video_background(void) {
    native_draw_background_video();
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
