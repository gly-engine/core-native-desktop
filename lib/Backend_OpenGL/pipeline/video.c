#include <string.h>

#include <libavutil/pixfmt.h>

#include "gefilter.h"
#include "gehook.h"
#include "gemedia.h"
#include "geopengl.h"

void native_draw_background_video(void) {
    GLBackendState *s = geogl_get_state();
    MediaFrame *f = avlib_get_background_frame();
    if (!f || !f->data[0]) {
        if (s->video_textures[0] != 0) {
            glDeleteTextures(3, s->video_textures);
            memset(s->video_textures, 0, sizeof(s->video_textures));
            s->video_width = s->video_height = s->video_format = 0;
        }
        glClearColor(s->clear_color[0], s->clear_color[1], s->clear_color[2], s->clear_color[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    bool yuv = (f->format == AV_PIX_FMT_YUV420P);
    if (s->video_textures[0] == 0 || s->video_width != f->width || s->video_height != f->height || s->video_format != f->format) {
        s->video_width = f->width; s->video_height = f->height; s->video_format = f->format;
        if (s->video_textures[0] != 0) glDeleteTextures(3, s->video_textures);
        if (yuv) {
            glGenTextures(3, s->video_textures);
            for (int i = 0; i < 3; i++) {
                glBindTexture(GL_TEXTURE_2D, s->video_textures[i]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                int w = (i == 0) ? f->width : f->width / 2;
                int h = (i == 0) ? f->height : f->height / 2;
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, f->data[i]);
            }
        } else {
            glGenTextures(1, &s->video_textures[0]);
            glBindTexture(GL_TEXTURE_2D, s->video_textures[0]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f->width, f->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, f->data[0]);
        }
    } else {
        if (yuv) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            for (int i = 0; i < 3; i++) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, s->video_textures[i]);
                int w = (i == 0) ? f->width : f->width / 2;
                int h = (i == 0) ? f->height : f->height / 2;
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE, f->data[i]);
            }
        } else {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s->video_textures[0]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, GL_RGBA, GL_UNSIGNED_BYTE, f->data[0]);
        }
    }
    glUseProgram(s->video_program);
    
    static const float identity[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
    glUniformMatrix4fv(s->video_loc_proj, 1, GL_FALSE, identity);

    if (yuv) {
        glUniform1i(s->video_loc_format, 1);
        for(int i=0; i<3; i++) {glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D, s->video_textures[i]);}
        glUniform1i(s->video_loc_tex_y, 0); glUniform1i(s->video_loc_tex_u, 1); glUniform1i(s->video_loc_tex_v, 2);
    } else {
        glUniform1i(s->video_loc_format, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s->video_textures[0]);
        glUniform1i(s->video_loc_tex_rgba, 0);
    }

    gecnd_filter_t *filter = gecnd_filter_get_config();
    
    glUniform1f(s->video_loc_time, (float)platform_get_time());
    glUniform1f(s->video_loc_bright, filter->brightness);
    glUniform1f(s->video_loc_contrast, filter->contrast);
    glUniform1f(s->video_loc_sat, filter->saturation);
    glUniform1f(s->video_loc_grain, filter->film_grain);
    glUniform1f(s->video_loc_scratch, filter->scratch_amount);
    glUniform1f(s->video_loc_jitter, filter->jitter_amount);

    glBindBuffer(GL_ARRAY_BUFFER, s->video_vbo);
    if (filter->video_dirty) {
        glBufferData(GL_ARRAY_BUFFER, sizeof(filter->video_vertices), filter->video_vertices, GL_DYNAMIC_DRAW);
    }
    
    glEnableVertexAttribArray(s->video_loc_pos);
    glEnableVertexAttribArray(s->video_loc_texCoord);
    glVertexAttribPointer(s->video_loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(s->video_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableVertexAttribArray(s->video_loc_pos);
    glDisableVertexAttribArray(s->video_loc_texCoord);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glClear(GL_DEPTH_BUFFER_BIT);
}
