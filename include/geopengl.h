#ifndef GEC_BACKEND_GL_INTERNAL_H
#define GEC_BACKEND_GL_INTERNAL_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

#include "kvec.h"
#include "gecnd.h"

#include <glad/gl.h>
#define GE_LINE_WIDTH 2.0f

/* GLES-only extension token (GL_OES_compressed_ETC1_RGB8_texture); glad/gl.h
 * (desktop GL 2.1 core) never declares it. Every window backend loads its
 * function table with gladLoadGL regardless of whether the live context is
 * desktop GL or GLES2 (both expose the same core entry points by name via
 * *GetProcAddress), so this constant is only ever exercised when a GLES2
 * context's extension string actually reports it (see ge_detect_etc1_support). */
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

// Metaatlas Size
#define GE_ATLAS_SIZE 2048
// Font Atlas part (within metaatlas)
#define GE_FONT_ATLAS_SIZE 1024
// YUV atlas: Y plane is full-res, chroma (Cb/Cr) is half-res. Both planes share
// the SAME normalized UV, so the shader samples all three at one coordinate.
#define GE_YUV_ATLAS_SIZE  GE_ATLAS_SIZE
#define GE_YUV_CHROMA_SIZE (GE_ATLAS_SIZE / 2)
// Max vertices in a single batch (multiple of 6, approx 256KB)
#define GE_MAX_VERTICES 8190
// Max layers for 2D depth sorting
#define GE_MAX_LAYERS 4096

typedef struct {
    GLuint id;
    int width;
    int height;
    int atlas_x;
    int atlas_y;
    int live_idx;
    float u, v;
    float u2, v2;
    bool is_opaque;
    int page_index;
    GECNDColorFormat color_format;
} GLTexture;

typedef struct {
    int x, y, w, h;
} GEAtlasRect;

/* Per-window-system function table. Each window/*.c backend (glfw.c, egl.c,
 * future wayland.c) implements one of these and hands it to ge_window_ops_set
 * right after its context is current, so the platform_* entry points called
 * from the rest of Backend_OpenGL (draw.c, video.c, libretro_hw.c) dispatch
 * to whichever backend actually won runtime selection instead of each window
 * backend defining its own copy of platform_swap_buffers/get_time/etc and
 * colliding at link time now that all of them are always built in. */
typedef struct {
    void   (*swap_buffers)(void);
    double (*get_time)(void);
    void*  (*get_proc_address)(const char *name);
    void   (*poll_should_close)(bool *should_close);
    void   (*terminate)(void);
} GEWindowOps;

/* One atlas page. The texture handle(s) live in an anonymous union keyed by
 * color_format: single texture (rgba8888/rgba5551), the YUV420P triplet, or a
 * vec of standalone ETC1 textures (ownership only — ETC1 binds by raw tex id).
 * Cursors/free_rects drive the shelf allocator for the atlased formats. */
typedef struct {
    GECNDColorFormat color_format;
    bool is_opaque;
    union {
        GLuint tex_id;                            /* rgba8888 / rgba5551 */
        struct { GLuint tex_y, tex_u, tex_v; };   /* yuv420 */
        kvec_t(GLuint) etc1_texs;                 /* etc1 (ownership) */
    };
    int cursor_x;
    int cursor_y;
    int row_height;
    int reset_cursor_x;
    int reset_cursor_y;
    int reset_row_height;
    kvec_t(GEAtlasRect) free_rects;
} GEAtlasPage;

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
    int16_t x, y, z, w;
    int16_t px, py;
    int16_t hw, hh;
    int16_t radius, pad;
    uint8_t r, g, b, a;
} GEDShapeComplexVertex;

typedef struct __attribute__((packed))
{
    int16_t x, y, z, w;
    uint8_t r, g, b, a;
} GESimpleShapeVertex;

typedef struct __attribute__((packed))
{
    int16_t x, y, z, w;
    uint8_t r, g, b, a;
    uint16_t u, v;
} GEAtlasVertex;

#include <assert.h>
#ifndef static_assert
#define static_assert _Static_assert
#endif

#define GE_MAX_CHUNK 1020

typedef enum {
    GE_PROG_SIMPLE    = 0,
    GE_PROG_COMPLEX   = 1,
    GE_PROG_ATLAS     = 2,
    GE_PROG_VIDEO     = 3,  // YUV420P fullscreen video: 3 textures + color filters
    GE_PROG_ATLAS_YUV = 4,  // YUV420P atlas images: 3 textures, YCbCr->RGB in fs
    GE_PROG_ALPHA8    = 5,  // single-channel coverage atlas: rgb=tint, a=tex.a*tint.a
    GE_PROG_COUNT     = 6,  // total size of programs[] and batch arrays
} GEProgramType;

typedef struct {
    GLuint id;
    GLint loc_proj;
    GLint loc_tex;
    GLint loc_size;
    GLint loc_thickness;
    GLint loc_aa_blur;
    GLint loc_jitter;
    
    // Video/Filter specific
    GLint loc_tex_y;
    GLint loc_tex_u;
    GLint loc_tex_v;
    GLint loc_format;
    GLint loc_brightness;
    GLint loc_contrast;
    GLint loc_saturation;
    GLint loc_film_grain;
    GLint loc_time;
    GLint loc_scratch;
    GLint loc_crt;
} GEProgram;

typedef struct {
    void *buffer;
    int count;
    int page_index;
} GEBatch;

typedef struct {
    void *window;
    uint16_t window_width;
    uint16_t window_height;
    double last_frame_time;

    GEProgram programs[GE_PROG_COUNT];
    GEProgram post_program;
    GLuint vbo_simple, vbo_complex, vbo_atlas, vbo_post;
    
    GLuint fbo_id;
    GLuint fbo_tex;
    int fbo_width, fbo_height;

    kvec_t(GEAtlasPage) atlas_pages;   /* unified: all color formats */
    bool etc1_supported;
    int active_opaque_page_index;
    int active_transparent_page_index;
    
    bool   atlas_dirty;
    float white_uv[2]; 
    float corner_uv[4]; // u, v, u2, v2
    int corner_page_index;

    float projection[16];
    GEColor current_color;

    kvec_t(GLTexture) textures;

    GEBatch opaque_batches[GE_PROG_COUNT];
    GEBatch transparent_batches[GE_PROG_COUNT];
    int16_t current_z;

    // Video State
    GLuint video_tex[3]; // Y, U, V (or just [0] for RGBA)
    atomic_int video_update_counter;
    int video_width;
    int video_height;
    int video_format;

    bool is_gles; // true when running on OpenGL ES (e.g. Mali 400)

    // HW Render (cores like PCSX ReARMed that render via OpenGL/GLES)
    GLuint hw_fbo_id;
    GLuint hw_fbo_tex;
    GLuint hw_fbo_depth_rb;
    int hw_fbo_width;
    int hw_fbo_height;
    bool hw_render_active;       // true after first RETRO_HW_FRAME_BUFFER_VALID
    bool hw_bottom_left_origin;  // core uses GL bottom-left UV convention

    GEWindowOps ops; // set by whichever window/*.c backend won runtime selection
} GLBackendState;

GLBackendState* geogl_get_state(void);
void init_all_shaders(bool is_gles);
void terminate_all_shaders(void);

void ge_pipeline_init(uint16_t w, uint16_t h);
void ge_pipeline_terminate(void);
void ge_pipeline_resize(uint16_t w, uint16_t h);
void ge_pipeline_start(void);
void ge_pipeline_flush(void);
void ge_pipeline_flush_primitives(void);
void    ge_zindex_reset(void);
int16_t ge_zindex_get(GEProgramType prog, bool opaque, int page_index);

/* Atlas manager (pipeline/atlas.c). One unified page list keyed by color format;
 * init/terminate own the kvec and GL textures. */
void ge_atlas_init(void);
void ge_atlas_terminate(void);
/* Appends a fresh page of (fmt, opaque) and returns its index. */
int  ge_atlas_create_page(GECNDColorFormat fmt, bool opaque);

/* Atlased formats (rgba8888/rgba5551/yuv420): find-or-create a page matching
 * (fmt, opaque) and carve a slot. For YUV the chroma slot is (ox/2,oy/2). */
void ge_atlas_acquire(GECNDColorFormat fmt, bool opaque, int w, int h,
                      int *page_index, int *ox, int *oy);
void ge_atlas_release(int page_index, int ox, int oy, int w, int h);
void ge_atlas_reset_images(void);

/* ETC1 standalone textures: owned by the single ETC1 page (mass-freed at
 * terminate / clear). Binding is by raw tex id (see flag below). */
void ge_atlas_etc1_add   (GLuint tex);
void ge_atlas_etc1_remove(GLuint tex);
void ge_atlas_etc1_clear (void);

/* ETC1 images bind by raw GL texture id, not an atlas page index — the batch
 * carries `page_index = tex | GECND_ATLAS_ETC_PAGE_FLAG`. Atlased formats use
 * page_index as a direct index into atlas_pages. */
#define GECND_ATLAS_ETC_PAGE_FLAG 0x10000

bool ge_detect_etc1_support(void);

void ge_batch_add_vertex_tex(int16_t x, int16_t y, float u, float v, uint32_t color, bool opaque, int page_index);
void ge_batch_add_vertex_alpha(int16_t x, int16_t y, float u, float v, uint32_t color, int page_index);
void ge_batch_add_vertex_yuv(int16_t x, int16_t y, float u, float v, uint32_t color, int page_index);
void ge_batch_add_vertex_shape(int16_t x, int16_t y, int16_t lx, int16_t ly, int16_t radius, uint32_t color, int8_t mode, bool aa);
void ge_batch_add_vertex_complex(float x, float y, float px, float py, float hw, float hh, float radius, uint32_t color, float mode);

void native_draw_background_video(void);
void native_text_terminate(void);

void platform_swap_buffers(void);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);
void ge_hw_register(void);

/* Shared window-backend plumbing (lib/Backend_OpenGL/window/common.c).
 * A window/*.c backend calls ge_window_ops_set once its context is current,
 * then ge_backend_ready to run the (backend-agnostic) pipeline/shader/blend
 * bring-up that used to be duplicated in every window backend's pre_init. */
void ge_window_ops_set(const GEWindowOps *ops);
void ge_backend_ready(gecnd_t *gly, uint16_t width, uint16_t height, bool is_gles);

/* Cheap availability probe (dlopen + immediate dlclose, no side effects) that
 * window backends use to decide whether to register a backend:* variant at
 * all, so runtime selection (see Frontend_Core/update.c pick_first_backend)
 * only ever sees variants whose shared libs are actually present on this
 * machine ("conforme as dll disponiveis"). */
bool ge_lib_available(const char *soname);

#endif
