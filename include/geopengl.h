#ifndef GEC_BACKEND_GL_INTERNAL_H
#define GEC_BACKEND_GL_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

#include "kvec.h"
#include "gecnd.h"

#if !defined(GECND_OPENGLES)
#error WTF?
#elif GECND_OPENGLES == 1
#include <glad/egl.h>
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#endif

typedef struct {
    GLuint id;
    int width;
    int height;
} GLTexture;

static inline void mat4_ortho(float *mat, float left, float right, float bottom, float top, float near, float far) {
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

static inline void set_color_from_u32(float *v, uint32_t c) {
    v[0] = ((c >> 24) & 0xFF) / 255.0f;
    v[1] = ((c >> 16) & 0xFF) / 255.0f;
    v[2] = ((c >>  8) & 0xFF) / 255.0f;
    v[3] = ( c        & 0xFF) / 255.0f;
}

#define GE_LINE_WIDTH 2.0f

typedef struct {
    void *window;
    uint16_t window_width;
    uint16_t window_height;
    double last_frame_time;

    GLuint shape_program;
    GLint shape_loc_pos;
    GLint shape_loc_proj;
    GLint shape_loc_color;
    GLint shape_loc_rect;
    GLint shape_loc_radius;
    GLint shape_loc_mode;
    GLint shape_loc_thickness;

    GLuint line_program;
    GLint line_loc_pos;
    GLint line_loc_proj;
    GLint line_loc_color;

    GLuint texture_program;
    GLint texture_loc_pos;
    GLint texture_loc_texCoord;
    GLint texture_loc_proj;
    GLint texture_loc_sampler;

    GLuint video_program;
    GLint video_loc_pos;
    GLint video_loc_texCoord;
    GLint video_loc_proj;
    GLint video_loc_tex_rgba;
    GLint video_loc_tex_y;
    GLint video_loc_tex_u;
    GLint video_loc_tex_v;
    GLint video_loc_format;

    GLuint font_program;
    GLint font_loc_pos;
    GLint font_loc_texCoord;
    GLint font_loc_proj;
    GLint font_loc_sampler;
    GLint font_loc_color;

    GLuint aa_program;
    GLint aa_loc_pos, aa_loc_texCoord, aa_loc_proj, aa_loc_sampler;
    GLint aa_loc_bright, aa_loc_contrast, aa_loc_sat, aa_loc_grain, aa_loc_tsize;
    GLint aa_loc_blur, aa_loc_wC, aa_loc_wN, aa_loc_sharpen;

    GLuint vbo;
    GLuint aa_fbo;
    GLuint aa_fbo_texture;

    float projection[16];
    float current_color[4];
    float clear_color[4];

    kvec_t(GLTexture) textures;
    GLuint video_textures[3];
    int video_width, video_height, video_format;
} GLBackendState;

GLBackendState* geogl_get_state(void);
void init_all_shaders(bool is_gles);
void terminate_all_shaders(void);

void ge_pipeline_init(uint16_t w, uint16_t h);
void ge_pipeline_start(void);
void ge_pipeline_flush(void);

void native_draw_background_video(void);
void native_text_terminate(void);

void platform_swap_buffers(void);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);

#endif
