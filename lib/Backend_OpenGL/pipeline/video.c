#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "gecnd.h"
#include "geopengl.h"
#include "gefilter.h"

static void update_video_textures(GLBackendState *s, MediaFrame *f) {
    bool need_realloc = (s->video_width != f->width || s->video_height != f->height || s->video_format != f->format);

    if (need_realloc) {
        if (s->video_tex[0]) glDeleteTextures(3, s->video_tex);
        glGenTextures(3, s->video_tex);

        s->video_width  = f->width;
        s->video_height = f->height;
        s->video_format = f->format;

        for (int i = 0; i < 3; i++) {
            glBindTexture(GL_TEXTURE_2D, s->video_tex[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        // Allocate once, stream updates via texsubimage
        // GL_ALPHA: 1 byte/pixel, universally supported on both GL and GLES 2.0.
        // Shader reads .a channel for Y/U/V planes.
        if (f->format == GECND_PIX_FMT_YUV420P) {
            glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, f->width, f->height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
            glBindTexture(GL_TEXTURE_2D, s->video_tex[1]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, f->width / 2, f->height / 2, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
            glBindTexture(GL_TEXTURE_2D, s->video_tex[2]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, f->width / 2, f->height / 2, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
        } else {
            glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
            GLenum fmt  = (f->format == GECND_PIX_FMT_RGB565) ? GL_RGB  : GL_RGBA;
            GLenum type = (f->format == GECND_PIX_FMT_RGB565) ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, f->width, f->height, 0, fmt, type, NULL);
        }
    }

    // alignment 1: tightly-packed data from set_buffer.c, handles odd widths
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (f->format == GECND_PIX_FMT_YUV420P) {
        glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, GL_ALPHA, GL_UNSIGNED_BYTE, f->data[0]);
        glBindTexture(GL_TEXTURE_2D, s->video_tex[1]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width / 2, f->height / 2, GL_ALPHA, GL_UNSIGNED_BYTE, f->data[1]);
        glBindTexture(GL_TEXTURE_2D, s->video_tex[2]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width / 2, f->height / 2, GL_ALPHA, GL_UNSIGNED_BYTE, f->data[2]);
    } else {
        glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
        GLenum fmt  = (f->format == GECND_PIX_FMT_RGB565) ? GL_RGB  : GL_RGBA;
        GLenum type = (f->format == GECND_PIX_FMT_RGB565) ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, fmt, type, f->data[0]);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void native_draw_background_video(void) {
    GLBackendState *s = geogl_get_state();

    // HW render path: core already drew into hw_fbo_tex on GPU
    if (s->hw_render_active && s->hw_fbo_tex) {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);

        int16_t W = (int16_t)s->window_width;
        int16_t H = (int16_t)s->window_height;

        // OpenGL FBOs are bottom-left origin; flip V when core set bottom_left_origin=true
        uint16_t v0 = s->hw_bottom_left_origin ? 65535 : 0;
        uint16_t v1 = s->hw_bottom_left_origin ? 0     : 65535;

        GEAtlasVertex quad[6] = {
            {0, 0, 0, 1, 255,255,255,255, 0,     v0},
            {W, 0, 0, 1, 255,255,255,255, 65535, v0},
            {W, H, 0, 1, 255,255,255,255, 65535, v1},
            {0, 0, 0, 1, 255,255,255,255, 0,     v0},
            {W, H, 0, 1, 255,255,255,255, 65535, v1},
            {0, H, 0, 1, 255,255,255,255, 0,     v1},
        };

        GEProgram *p = &s->programs[GE_PROG_ATLAS];
        glUseProgram(p->id);
        glUniformMatrix4fv(p->loc_proj, 1, GL_FALSE, s->projection);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->hw_fbo_tex);
        glUniform1i(p->loc_tex, 0);

        size_t stride = sizeof(GEAtlasVertex);
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo_atlas);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_SHORT,          GL_FALSE, stride, (void*)offsetof(GEAtlasVertex, x));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE,  GL_TRUE,  stride, (void*)offsetof(GEAtlasVertex, r));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE,  stride, (void*)offsetof(GEAtlasVertex, u));
        glDisableVertexAttribArray(3);
        glDisableVertexAttribArray(4);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        return;
    }

    // Software render path
    MediaFrame *f = gamely_daemon_media_background_get_frame();
    if (!f) return;

    if (gamely_daemon_media_background_check_update(&s->video_update_counter)) {
        update_video_textures(s, f);
    }

    if (!s->video_tex[0]) return;

    gecnd_filter_t *filter = gecnd_filter_get_config();

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    if (f->format == GECND_PIX_FMT_YUV420P) {
        // YUV420P: dedicated video program with NDC fullscreen quad + color filters
        static const float fullscreen_ndc[24] = {
            -1.0f,  1.0f,  0.0f, 0.0f,  // TL
             1.0f,  1.0f,  1.0f, 0.0f,  // TR
             1.0f, -1.0f,  1.0f, 1.0f,  // BR
            -1.0f,  1.0f,  0.0f, 0.0f,  // TL
             1.0f, -1.0f,  1.0f, 1.0f,  // BR
            -1.0f, -1.0f,  0.0f, 1.0f,  // BL
        };
        static const float identity[16] = {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1
        };
        const float *verts = gencd_filter_is_zero_video_pos() ? fullscreen_ndc : filter->video_vertices;

        GEProgram *p = &s->programs[GE_PROG_VIDEO];
        glUseProgram(p->id);
        glUniformMatrix4fv(p->loc_proj, 1, GL_FALSE, identity);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s->video_tex[0]); glUniform1i(p->loc_tex_y, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, s->video_tex[1]); glUniform1i(p->loc_tex_u, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, s->video_tex[2]); glUniform1i(p->loc_tex_v, 2);
        glUniform1f(p->loc_brightness,  filter->brightness);
        glUniform1f(p->loc_contrast,    filter->contrast);
        glUniform1f(p->loc_saturation,  filter->saturation);
        glUniform1f(p->loc_film_grain,  filter->film_grain);
        glUniform1f(p->loc_time,        (float)platform_get_time());
        glUniform1f(p->loc_scratch,     filter->scratch_amount);
        glUniform1f(p->loc_jitter,      filter->jitter_amount);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts + 2);
        glDisableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(2);

    } else {
        // RGBA / RGB565: reuse atlas program (tex * white = tex), pixel-space fullscreen quad
        int16_t W = (int16_t)s->window_width;
        int16_t H = (int16_t)s->window_height;
        GEAtlasVertex quad[6] = {
            {0, 0, 0, 1, 255,255,255,255, 0,     0    },  // TL
            {W, 0, 0, 1, 255,255,255,255, 65535, 0    },  // TR
            {W, H, 0, 1, 255,255,255,255, 65535, 65535},  // BR
            {0, 0, 0, 1, 255,255,255,255, 0,     0    },  // TL
            {W, H, 0, 1, 255,255,255,255, 65535, 65535},  // BR
            {0, H, 0, 1, 255,255,255,255, 0,     65535},  // BL
        };

        GEProgram *p = &s->programs[GE_PROG_ATLAS];
        glUseProgram(p->id);
        glUniformMatrix4fv(p->loc_proj, 1, GL_FALSE, s->projection);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->video_tex[0]);
        glUniform1i(p->loc_tex, 0);

        size_t stride = sizeof(GEAtlasVertex);
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo_atlas);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_SHORT,          GL_FALSE, stride, (void*)offsetof(GEAtlasVertex, x));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE,  GL_TRUE,  stride, (void*)offsetof(GEAtlasVertex, r));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE,  stride, (void*)offsetof(GEAtlasVertex, u));
        glDisableVertexAttribArray(3);
        glDisableVertexAttribArray(4);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
}
