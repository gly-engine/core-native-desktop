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
    bool is_opaque;
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

typedef struct __attribute__((packed))
{
    float x, y, z;
    float lx, ly;
    float mode, radius;
    uint8_t r, g, b, a;
} GEDShapeComplexVertex;

typedef struct __attribute__((packed))
{
    float x, y, z;
    uint8_t r, g, b, a;
} GESimpleShapeVertex;

typedef struct __attribute__((packed))
{
    float x, y, z;
    uint8_t r, g, b, a;
    float u0, v0;
    float u1, v1;
} GEAtlasVertex;

#include <assert.h>
#ifndef static_assert
#define static_assert _Static_assert
#endif

#define GE_MAX_CHUNK 1020

typedef enum {
    GE_PROG_SIMPLE,
    GE_PROG_COMPLEX,
    GE_PROG_ATLAS,
    GE_PROG_COUNT
} GEProgramType;

typedef struct {
    GLuint id;
    GLint loc_proj;
    GLint loc_tex;
    GLint loc_size;
    GLint loc_thickness;
    GLint loc_aa_blur;
    GLint loc_jitter;
} GEProgram;

typedef struct {
    void *buffer;
    int count;
} GEBatch;

typedef struct {
    void *window;
    uint16_t window_width;
    uint16_t window_height;
    double last_frame_time;

    GEProgram programs[GE_PROG_COUNT];
    GLuint vbo_simple, vbo_complex, vbo_atlas;
    
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

    GEBatch opaque_batches[GE_PROG_COUNT];
    GEBatch transparent_batches[GE_PROG_COUNT];
    int16_t current_z;
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

void ge_batch_add_vertex_tex(int16_t x, int16_t y, int16_t u, int16_t v, uint32_t color, bool opaque);
void ge_batch_add_vertex_shape(int16_t x, int16_t y, int16_t lx, int16_t ly, int16_t radius, uint32_t color, int8_t mode, bool aa);

void ge_batch_get_color_u8(uint8_t *c);

void native_text_terminate(void);

void platform_swap_buffers(void);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);

#endif
