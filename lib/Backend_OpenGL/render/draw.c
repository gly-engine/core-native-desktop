#include <stdio.h>
#include <string.h>
#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"

void native_draw_start(void) {
    ge_pipeline_start();
}

void native_draw_flush(void) {
    ge_pipeline_flush();
    platform_swap_buffers();
}

void native_draw_color(uint32_t color) {
    set_color_from_u32(geogl_get_state()->current_color, color);
}

void native_draw_clear(uint32_t color) {
    set_color_from_u32(geogl_get_state()->clear_color, color);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *s = geogl_get_state();
    float v[8] = {(float)x, (float)y, (float)x+w, (float)y, (float)x+w, (float)y+h, (float)x, (float)y+h};
    glUseProgram(s->shape_program);
    glUniformMatrix4fv(s->shape_loc_proj, 1, GL_FALSE, s->projection);
    glUniform4fv(s->shape_loc_color, 1, s->current_color);
    float mr = (w < h ? (float)w : (float)h) / 2.0f;
    float fr = (float)r > mr ? mr : (float)r;
    float ru[4] = {(float)x, (float)y, (float)w, (float)h};
    glUniform4fv(s->shape_loc_rect, 1, ru);
    glUniform1f(s->shape_loc_radius, fr);
    glUniform1i(s->shape_loc_mode, mode);
    glUniform1f(s->shape_loc_thickness, GE_LINE_WIDTH);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glEnableVertexAttribArray(s->shape_loc_pos);
    glVertexAttribPointer(s->shape_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableVertexAttribArray(s->shape_loc_pos);
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    GLBackendState *s = geogl_get_state();
    float v[4] = {(float)x1, (float)y1, (float)x2, (float)y2};
    glUseProgram(s->line_program);
    glUniformMatrix4fv(s->line_loc_proj, 1, GL_FALSE, s->projection);
    glUniform4fv(s->line_loc_color, 1, s->current_color);
    glLineWidth(GE_LINE_WIDTH);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glEnableVertexAttribArray(s->line_loc_pos);
    glVertexAttribPointer(s->line_loc_pos, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(s->line_loc_pos);
}
