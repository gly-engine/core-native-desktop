#include "gehook.h"
#include "geopengl.h"
#include "gemedia.h"
#include <libavutil/pixfmt.h>

void native_draw_background_video(void) {
    GLBackendState *state = geogl_get_state();
    MediaFrame *frame = avlib_get_background_frame();

    if (!frame || !frame->data[0]) {
        if (state->video_textures[0] != 0) {
            glDeleteTextures(3, state->video_textures);
            memset(state->video_textures, 0, sizeof(state->video_textures));
            state->video_width = 0;
            state->video_height = 0;
            state->video_format = 0;
        }
        glClearColor(state->clear_color[0], state->clear_color[1], state->clear_color[2], state->clear_color[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    bool is_yuv = (frame->format == AV_PIX_FMT_YUV420P);

    if (state->video_textures[0] == 0 || state->video_width != frame->width || state->video_height != frame->height || state->video_format != frame->format) {
        state->video_width = frame->width;
        state->video_height = frame->height;
        state->video_format = frame->format;

        if (state->video_textures[0] != 0) {
            glDeleteTextures(3, state->video_textures);
            memset(state->video_textures, 0, sizeof(state->video_textures));
        }
        
        if (is_yuv) {
            glGenTextures(3, state->video_textures);
            for (int i = 0; i < 3; i++) {
                glBindTexture(GL_TEXTURE_2D, state->video_textures[i]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                int w = (i == 0) ? frame->width : frame->width / 2;
                int h = (i == 0) ? frame->height : frame->height / 2;
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[i]);
            }
        } else { // RGBA or other packed formats
            glGenTextures(1, &state->video_textures[0]);
            glBindTexture(GL_TEXTURE_2D, state->video_textures[0]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            // Assuming RGBA for non-YUV formats for now
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame->width, frame->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->data[0]);
        }
    } else {
        if (is_yuv) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            for (int i = 0; i < 3; i++) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, state->video_textures[i]);
                int w = (i == 0) ? frame->width : frame->width / 2;
                int h = (i == 0) ? frame->height : frame->height / 2;
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[i]);
            }
        } else { // RGBA
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, state->video_textures[0]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RGBA, GL_UNSIGNED_BYTE, frame->data[0]);
        }
    }

    glUseProgram(state->video_program);
    glUniformMatrix4fv(state->video_loc_proj, 1, GL_FALSE, state->projection);

    if (is_yuv) {
        glUniform1i(state->video_loc_format, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state->video_textures[0]);
        glUniform1i(state->video_loc_tex_y, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, state->video_textures[1]);
        glUniform1i(state->video_loc_tex_u, 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, state->video_textures[2]);
        glUniform1i(state->video_loc_tex_v, 2);
    } else {
        glUniform1i(state->video_loc_format, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state->video_textures[0]);
        glUniform1i(state->video_loc_tex_rgba, 0);
    }

    float w = (float)state->window_width;
    float h = (float)state->window_height;
    float vertices[] = { 0.0f, 0.0f, 0.0f, 0.0f, w, 0.0f, 1.0f, 0.0f, w, h, 1.0f, 1.0f, 0.0f, h, 0.0f, 1.0f };
    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

    glEnableVertexAttribArray(state->video_loc_pos);
    glVertexAttribPointer(state->video_loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(state->video_loc_texCoord);
    glVertexAttribPointer(state->video_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(state->video_loc_pos);
    glDisableVertexAttribArray(state->video_loc_texCoord);
    
    // Restore GL state for other draw calls (e.g., text rendering)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    glClear(GL_DEPTH_BUFFER_BIT);
}
