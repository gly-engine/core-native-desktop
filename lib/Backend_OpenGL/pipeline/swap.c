#include <stdlib.h>
#include <string.h>

#include "gefilter.h"
#include "geopengl.h"

void ge_pipeline_resize(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w;
    s->window_height = h;
    glBindTexture(GL_TEXTURE_2D, s->post_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    
    // Refresh projections when window resizes
    gecnd_filter_reset_corners();
    gecnd_filter_reset_video_pos();
}

void ge_pipeline_start(void) {
    GLBackendState *s = geogl_get_state();
    glBindFramebuffer(GL_FRAMEBUFFER, s->post_fbo);
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
    glUseProgram(s->post_program);
    
    static const float identity[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
    glUniformMatrix4fv(s->post_loc_proj, 1, GL_FALSE, identity);
    glUniform2f(s->post_loc_tsize, 1.0f / (float)s->window_width, 1.0f / (float)s->window_height);
    
    glUniform1f(s->post_loc_rotation, filter->rotation_rad);
    glUniform2f(s->post_loc_center, (float)s->window_width / 2.0f, (float)s->window_height / 2.0f);
    glUniform1f(s->post_loc_crt, filter->crt_amount);
    glUniform1f(s->post_loc_time, (float)platform_get_time());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->post_fbo_texture);
    glUniform1i(s->post_loc_sampler, 0);

    glBindBuffer(GL_ARRAY_BUFFER, s->post_vbo);
    if (filter->post_dirty) {
        glBufferData(GL_ARRAY_BUFFER, sizeof(filter->corner_vertices), filter->corner_vertices, GL_DYNAMIC_DRAW);
    }
    
    glEnableVertexAttribArray(s->post_loc_pos);
    glEnableVertexAttribArray(s->post_loc_texCoord);
    glVertexAttribPointer(s->post_loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(s->post_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableVertexAttribArray(s->post_loc_pos);
    glDisableVertexAttribArray(s->post_loc_texCoord);
    glEnable(GL_BLEND);

    // Reset dirty flags after use
    filter->post_dirty = false;
    filter->video_dirty = false;
}
