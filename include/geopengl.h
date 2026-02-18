#ifndef GEC_BACKEND_GL_INTERNAL_H
#define GEC_BACKEND_GL_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include "kvec.h" // For kvec_t

typedef struct {
    GLuint id;
    int width;
    int height;
} GLTexture;

/* =========================================
 * Inline Helpers
 * ========================================= */

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


/* =========================================
 * Backend Context
 * ========================================= */

#define GE_LINE_WIDTH 2.0f

typedef struct {
    // Window state from platform
    void *window; // Can be GLFWwindow* or EGLNativeWindowType
    uint16_t window_width;
    uint16_t window_height;
    double last_frame_time;

    // --- OpenGL State ---

    // Shape drawing program
    GLuint shape_program;
    GLint shape_loc_pos;
    GLint shape_loc_proj;
    GLint shape_loc_color;
    GLint shape_loc_rect;
    GLint shape_loc_radius;
    GLint shape_loc_mode;
    GLint shape_loc_thickness;

    // Line drawing program
    GLuint line_program;
    GLint line_loc_pos;
    GLint line_loc_proj;
    GLint line_loc_color;

    // Texture drawing program
    GLuint texture_program;
    GLint texture_loc_pos;
    GLint texture_loc_texCoord;
    GLint texture_loc_proj;
    GLint texture_loc_sampler;

    // Unified Video/Texture drawing program
    GLuint video_program;
    GLint video_loc_pos;
    GLint video_loc_texCoord;
    GLint video_loc_proj;
    GLint video_loc_tex_rgba;
    GLint video_loc_tex_y;
    GLint video_loc_tex_u;
    GLint video_loc_tex_v;
    GLint video_loc_format;

    // Font drawing program
    GLuint font_program;
    GLint font_loc_pos;
    GLint font_loc_texCoord;
    GLint font_loc_proj;
    GLint font_loc_sampler;
    GLint font_loc_color;

    // AA / Post-process program
    GLuint aa_program;
    GLint aa_loc_pos;
    GLint aa_loc_texCoord;
    GLint aa_loc_proj;
    GLint aa_loc_sampler;
    GLint aa_loc_texelSize;
    GLint aa_loc_blur;
    GLint aa_loc_weightCenter;
    GLint aa_loc_weightNeighbor;

    // Common GL objects
    GLuint vbo;
    GLuint aa_fbo;
    GLuint aa_fbo_texture;
    int aa_fbo_width;
    int aa_fbo_height;

    // --- Drawing State ---
    float projection[16];
    float current_color[4];
    float clear_color[4];

    // --- Texture Storage ---
    kvec_t(GLTexture) textures;

    // --- Video Background ---
    GLuint video_textures[3]; // 0: Y/RGBA, 1: U, 2: V
    int video_width;
    int video_height;
    int video_format;

} GLBackendState;

// Global accessor for the backend state
GLBackendState* geogl_get_state(void);


/* =========================================
 * Render Functions
 * ========================================= */

void native_draw_background_video(void);

/* =========================================
 * Platform Abstraction (to be implemented by glfw.c, egl.c, etc.)
 * ========================================= */

int platform_init(uint16_t width, uint16_t height);
void platform_terminate(void);
void platform_swap_buffers(void);
void platform_poll_events(void);
bool platform_should_close(void);
void platform_set_swap_interval(int interval);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);


#endif // GEC_BACKEND_GL_INTERNAL_H
