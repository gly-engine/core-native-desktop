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

// Metaatlas Size
#define GE_ATLAS_SIZE 2048
// Font Atlas part (within metaatlas)
#define GE_FONT_ATLAS_SIZE 1024
// Max vertices in a single batch (multiple of 6, approx 256KB)
#define GE_MAX_VERTICES 8190

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

typedef union {
    uint32_t u32;
    uint8_t rgba[4];
} GEColor;

typedef struct {
    float x, y;       // 8
    float u, v;       // 8
    GEColor color;    // 4 (RGBA)
    int8_t local[2];  // 2 (Mapped to -1.0..1.0 in shader)
    uint8_t sdf[2];   // 2 (Radius, Border - max 255px)
    float size[2];    // 8
} GEDrawVertex; // 32 bytes - perfectly aligned

#define GE_BATCH_SIZE (GE_MAX_VERTICES * sizeof(GEDrawVertex))
// Slicing for performance on Mali - Must be multiple of 3!
#define GE_MAX_CHUNK 1020

typedef struct {
    void *window;
    uint16_t window_width;
    uint16_t window_height;
    double last_frame_time;

    GLuint draw_program;
    GLuint current_program;
    GLint  draw_loc_proj;
    GLint  draw_loc_atlas;

    GLuint vbo;
    
    GLuint atlas_id;
    bool   atlas_dirty;
    int atlas_width;
    int atlas_height;
    int alloc_cursor_x;
    int alloc_cursor_y;
    int alloc_row_height;
    float white_uv[2]; 

    float projection[16];
    GEColor current_color;

    kvec_t(GLTexture) textures;

    GEDrawVertex *batch_buffer;
    int batch_count;
} GLBackendState;

GLBackendState* geogl_get_state(void);
void init_all_shaders(bool is_gles);
void terminate_all_shaders(void);

void ge_pipeline_init(uint16_t w, uint16_t h);
void ge_pipeline_terminate(void);
void ge_pipeline_resize(uint16_t w, uint16_t h);
void ge_pipeline_start(void);
void ge_pipeline_end(void);
void ge_pipeline_flush(void);
void ge_pipeline_flush_primitives(void);
void ge_atlas_alloc(int w, int h, int *ox, int *oy);
void ge_batch_add_vertex(float x, float y, float u, float v, uint32_t color, float lx, float ly, float sw, float sh, float r, float b);
void ge_batch_get_color_u8(uint8_t *c);

void native_text_terminate(void);

void platform_swap_buffers(void);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);

#endif
