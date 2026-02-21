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

typedef struct {
    GLuint id; // Atlas ID (redundant if one atlas, but keeps compatibility)
    int width;
    int height;
    float u, v;   // Top-left UV in atlas
    float u2, v2; // Bottom-right UV in atlas
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

typedef struct {
    float x, y;       // a_pos
    float u, v;       // a_uv
    uint8_t color[4]; // a_color
    float rect[4];    // a_rect (x, y, w, h) for SDF calculations
    float data[3];    // a_data (radius, border, outline)
} GEDrawVertex;

typedef struct {
    int x, y, w, h;
    bool used;
} AtlasNode;

typedef struct {
    void *window;
    uint16_t window_width;
    uint16_t window_height;
    double last_frame_time;

    GLuint draw_program;
    GLint  draw_loc_pos;
    GLint  draw_loc_uv;
    GLint  draw_loc_color;
    GLint  draw_loc_rect;
    GLint  draw_loc_data;
    GLint  draw_loc_proj;
    GLint  draw_loc_atlas;

    GLuint vbo;
    
    // Mega Atlas State
    GLuint atlas_id;
    int atlas_width;
    int atlas_height;
    
    // Atlas Allocator (Simple Shelf/Block for now)
    // We reserve 0-1024 for Fonts/System
    int alloc_cursor_x;
    int alloc_cursor_y;
    int alloc_row_height;

    // White pixel for solid shapes (located at 0,0)
    float white_uv[4]; 

    float projection[16];
    float current_color[4];
    float current_data[3]; // radius, border, outline
    float clear_color[4];

    // Images mapped to atlas regions
    kvec_t(GLTexture) textures; // storing atlas UVs/Rects inside GLTexture
    
    // Legacy Video (separate pipeline for now, or could be merged if we update subimage frequently)
    GLuint video_program;
    GLint  video_loc_pos;
    GLint  video_loc_texCoord;
    GLint  video_loc_proj;
    GLint  video_loc_tex_rgba;
    GLint  video_loc_tex_y;
    GLint  video_loc_tex_u;
    GLint  video_loc_tex_v;
    GLint  video_loc_format;
    GLint  video_loc_bright;
    GLint  video_loc_contrast;
    GLint  video_loc_sat;
    GLint  video_loc_grain;
    GLint  video_loc_sharpen;
    GLint  video_loc_tsize;
    GLint  video_loc_time;
    GLint  video_loc_scratch;
    GLint  video_loc_jitter;
    GLuint video_vbo;
    GLuint video_textures[3];
    int video_width, video_height, video_format;
    atomic_int video_update_count;

    // Post processing (FBO)
    GLuint post_program;
    GLint  post_loc_pos;
    GLint  post_loc_texCoord;
    GLint  post_loc_proj;
    GLint  post_loc_sampler;
    GLint  post_loc_tsize;
    GLint  post_loc_rotation;
    GLint  post_loc_center;
    GLint  post_loc_crt;
    GLint  post_loc_time;
    GLuint post_vbo;
    GLuint post_fbo;
    GLuint post_fbo_texture;

    GEDrawVertex *batch_buffer;
    int batch_count;
} GLBackendState;

GLBackendState* geogl_get_state(void);
void init_all_shaders(bool is_gles);
void terminate_all_shaders(void);

void ge_pipeline_init(uint16_t w, uint16_t h);
void ge_pipeline_start(void);
void ge_pipeline_flush(void);
void ge_pipeline_flush_primitives(void);
void ge_atlas_alloc(int w, int h, int *ox, int *oy);
void ge_batch_add_vertex(float x, float y, float u, float v, uint8_t *color, float *rect, float *data);
void ge_batch_get_color_u8(uint8_t *c);

void native_draw_background_video(void);
void native_text_terminate(void);

void platform_swap_buffers(void);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);

#endif
