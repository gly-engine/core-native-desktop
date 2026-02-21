#ifndef GEC_BACKEND_GL_INTERNAL_H
#define GEC_BACKEND_GL_INTERNAL_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

#include "kvec.h"
#include "gecnd.h"

#if !defined(GECND_OPENGLES)
#error WTF?
#elif GECND_OPENGLES == 1
#include <glad/egl.h>
#include <glad/gles2.h>
#define GE_LINE_WIDTH 2.0f
#else
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#define GE_LINE_WIDTH 2.0f
#endif

// 1MB batch buffer
#define GE_BATCH_SIZE (1024 * 1024)
// Slicing for performance on Mali
#define GE_MAX_CHUNK 4092

typedef struct {
    GLuint id;
    int width;
    int height;
    float u, v;
    float u2, v2;
} GLTexture;

static inline void mat4_ortho(float *mat, float left, float right, float bottom, float top, float near, float far) {
    mat[0] = 2.0f / (right - left); mat[1] = 0.0f; mat[2] = 0.0f; mat[3] = 0.0f;
    mat[4] = 0.0f; mat[5] = 2.0f / (top - bottom); mat[6] = 0.0f; mat[7] = 0.0f;
    mat[8] = 0.0f; mat[9] = 0.0f; mat[10] = -2.0f / (far - near); mat[11] = 0.0f;
    mat[12] = -(right + left) / (right - left); mat[13] = -(top + bottom) / (top - bottom);
    mat[14] = -(far + near) / (far - near); mat[15] = 1.0f;
}

static inline void set_color_from_u32(float *v, uint32_t c) {
    v[0] = ((c >> 24) & 0xFF) / 255.0f;
    v[1] = ((c >> 16) & 0xFF) / 255.0f;
    v[2] = ((c >>  8) & 0xFF) / 255.0f;
    v[3] = ( c        & 0xFF) / 255.0f;
}

typedef struct {
    float x, y;       // 8
    float u, v;       // 8
    float color[4];   // 16
    float local[2];   // 8
    float size[2];    // 8
    float sdf[2];     // 8
    float padding[2]; // 8
} GEDrawVertex; // 64 bytes - 16-byte aligned

typedef struct {
    void *window;
    uint16_t window_width;
    uint16_t window_height;
    double last_frame_time;

    GLuint draw_program;
    GLint  draw_loc_proj;
    GLint  draw_loc_atlas;

    GLuint vbos[3]; 
    int vbo_idx;
    
    GLuint atlas_id;
    int atlas_width;
    int atlas_height;
    int alloc_cursor_x;
    int alloc_cursor_y;
    int alloc_row_height;
    float white_uv[2]; 

    float projection[16];
    float current_color[4];
    float clear_color[4];

    kvec_t(GLTexture) textures;
    
    GLuint video_program;
    GLint  video_loc_pos, video_loc_texCoord, video_loc_proj;
    GLint  video_loc_tex_rgba, video_loc_tex_y, video_loc_tex_u, video_loc_tex_v;
    GLint  video_loc_format, video_loc_bright, video_loc_contrast, video_loc_sat;
    GLint  video_loc_grain, video_loc_sharpen, video_loc_tsize, video_loc_time, video_loc_scratch, video_loc_jitter;
    GLuint video_vbo;
    GLuint video_textures[3];
    int video_width, video_height, video_format;
    atomic_int video_update_count;

    GLuint post_program;
    GLint  post_loc_pos, post_loc_texCoord, post_loc_proj, post_loc_sampler, post_loc_tsize;
    GLint  post_loc_rotation, post_loc_center, post_loc_crt, post_loc_time;
    GLuint post_vbo, post_fbo, post_fbo_texture;

    GEDrawVertex *batch_buffer;
    int batch_count;
} GLBackendState;

GLBackendState* geogl_get_state(void);
void init_all_shaders(bool is_gles);
void terminate_all_shaders(void);

void ge_pipeline_init(uint16_t w, uint16_t h);
void ge_pipeline_resize(uint16_t w, uint16_t h);
void ge_pipeline_start(void);
void ge_pipeline_flush(void);
void ge_pipeline_flush_primitives(void);
void ge_atlas_alloc(int w, int h, int *ox, int *oy);
void ge_batch_add_vertex(float x, float y, float u, float v, float* color, float lx, float ly, float sw, float sh, float r, float b);
void ge_batch_get_color_u8(uint8_t *c);

void native_draw_background_video(void);
void native_text_terminate(void);

void platform_swap_buffers(void);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);

#endif
