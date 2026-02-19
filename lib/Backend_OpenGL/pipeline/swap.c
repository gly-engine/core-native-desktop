#include <stdlib.h>
#include <string.h>
#include "gecnd.h"
#include "geopengl.h"

void ge_pipeline_start(void) {
    GLBackendState *s = geogl_get_state();
    glBindFramebuffer(GL_FRAMEBUFFER, s->aa_fbo);
    glViewport(0, 0, s->window_width, s->window_height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0, -1, 1);
    native_draw_background_video();
}

void ge_pipeline_flush(void) {
    GLBackendState *s = geogl_get_state();
    gecnd_filter_t *filter = gecnd_filter_get_config();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, s->window_width, s->window_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glUseProgram(s->aa_program);
    float ortho[16];
    mat4_ortho(ortho, 0, (float)s->window_width, (float)s->window_height, 0, -1, 1);
    glUniformMatrix4fv(s->aa_loc_proj, 1, GL_FALSE, ortho);
    glUniform1f(s->aa_loc_bright, filter->brightness);
    glUniform1f(s->aa_loc_contrast, filter->contrast);
    glUniform1f(s->aa_loc_sat, filter->saturation);
    glUniform1f(s->aa_loc_grain, filter->film_grain);
    glUniform1f(s->aa_loc_sharpen, filter->sharpen);
    glUniform1f(s->aa_loc_blur, filter->aa_blur);
    glUniform1f(s->aa_loc_wC, filter->aa_weight_center);
    glUniform1f(s->aa_loc_wN, filter->aa_weight_neighbor);
    glUniform2f(s->aa_loc_tsize, 1.0f / (float)s->window_width, 1.0f / (float)s->window_height);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->aa_fbo_texture);
    glUniform1i(s->aa_loc_sampler, 0);
    gecnd_vec2 *c = gecnd_filter_get_config()->corners;
    float vertices[] = {
        c[0].x, c[0].y, 0.0f, 1.0f,
        c[1].x, c[1].y, 1.0f, 1.0f,
        c[2].x, c[2].y, 1.0f, 0.0f,
        c[3].x, c[3].y, 0.0f, 0.0f
    };
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glEnableVertexAttribArray(s->aa_loc_pos);
    glEnableVertexAttribArray(s->aa_loc_texCoord);
    glVertexAttribPointer(s->aa_loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(s->aa_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableVertexAttribArray(s->aa_loc_pos);
    glDisableVertexAttribArray(s->aa_loc_texCoord);
    glEnable(GL_BLEND);
}
