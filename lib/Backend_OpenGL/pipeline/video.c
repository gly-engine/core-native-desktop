#include <stdio.h>
#include <string.h>
#include "geopengl.h"
#include "gebuffer.h"
#include "gefilter.h"

static void update_video_textures(GLBackendState *s, MediaFrame *f) {
    bool need_realloc = (s->video_width != f->width || s->video_height != f->height || s->video_format != f->format);

    if (need_realloc) {
        if (s->video_tex[0]) glDeleteTextures(3, s->video_tex);
        glGenTextures(3, s->video_tex);
        
        s->video_width = f->width;
        s->video_height = f->height;
        s->video_format = f->format;

        for (int i = 0; i < 3; i++) {
            glBindTexture(GL_TEXTURE_2D, s->video_tex[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        // alocate once, then just stream updates with texsubimage
        // never alocate things over and over again!
        if (f->format == GECND_PIX_FMT_YUV420P) {
            glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, f->width, f->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
            glBindTexture(GL_TEXTURE_2D, s->video_tex[1]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, f->width / 2, f->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
            glBindTexture(GL_TEXTURE_2D, s->video_tex[2]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, f->width / 2, f->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
        } else {
            glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
            GLenum fmt = (f->format == GECND_PIX_FMT_RGB565) ? GL_RGB : GL_RGBA;
            GLenum type = (f->format == GECND_PIX_FMT_RGB565) ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, f->width, f->height, 0, fmt, type, NULL);
        }
    }

    ge_pipeline_flush_primitives();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (f->format == GECND_PIX_FMT_YUV420P) {
        glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, GL_LUMINANCE, GL_UNSIGNED_BYTE, f->data[0]);
        glBindTexture(GL_TEXTURE_2D, s->video_tex[1]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width / 2, f->height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, f->data[1]);
        glBindTexture(GL_TEXTURE_2D, s->video_tex[2]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width / 2, f->height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, f->data[2]);
    } else {
        glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
        GLenum fmt = (f->format == GECND_PIX_FMT_RGB565) ? GL_RGB : GL_RGBA;
        GLenum type = (f->format == GECND_PIX_FMT_RGB565) ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, fmt, type, f->data[0]);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void native_draw_background_video(void) {
    return; /** @todo: enable */
    GLBackendState *s = geogl_get_state();
    MediaFrame *f = gecnd_get_background_frame();
    if (!f) return;

    if (gecnd_buffer_check_update(&s->video_update_counter)) {
        update_video_textures(s, f);
    }

    if (!s->video_tex[0]) return;

    gecnd_filter_t *filter = gecnd_filter_get_config();
    GEProgram *p = &s->programs[GE_PROG_VIDEO];

    glUseProgram(p->id);
    
    // Use true identity matrix for NDC vertices (-1..1)
    static const float identity[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
    glUniformMatrix4fv(p->loc_proj, 1, GL_FALSE, identity);

    // Textures
    if (f->format == GECND_PIX_FMT_YUV420P) {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s->video_tex[0]); glUniform1i(p->loc_tex_y, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, s->video_tex[1]); glUniform1i(p->loc_tex_u, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, s->video_tex[2]); glUniform1i(p->loc_tex_v, 2);
        glUniform1i(p->loc_format, 1);
    } else {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s->video_tex[0]); glUniform1i(p->loc_tex, 0);
        glUniform1i(p->loc_format, 0);
    }

    // Filters
    glUniform1f(p->loc_brightness, filter->brightness);
    glUniform1f(p->loc_contrast, filter->contrast);
    glUniform1f(p->loc_saturation, filter->saturation);
    glUniform1f(p->loc_film_grain, filter->film_grain);
    glUniform1f(p->loc_time, (float)platform_get_time());
    glUniform1f(p->loc_scratch, filter->scratch_amount);
    glUniform1f(p->loc_jitter, filter->jitter_amount);

    // Use pre-calculated corner_vertices (6 vertices for GL_TRIANGLES)
    glBindBuffer(GL_ARRAY_BUFFER, 0); 
    glEnableVertexAttribArray(0); // a_pos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), filter->corner_vertices);
    glEnableVertexAttribArray(2); // a_texCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &filter->corner_vertices[2]);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(2);
}