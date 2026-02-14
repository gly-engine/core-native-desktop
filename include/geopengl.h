#ifndef GEC_BACKEND_GL_INTERNAL_H
#define GEC_BACKEND_GL_INTERNAL_H

#include <glad/gl.h>
#include <stdint.h>
#include <stdbool.h>
#include "kvec.h" // For kvec_t

// Forward declaration for GLFW window
struct GLFWwindow;

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

typedef struct {
    // Window state from platform
    struct GLFWwindow *window;
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

    // Font drawing program
    GLuint font_program;
    GLint font_loc_pos;
    GLint font_loc_texCoord;
    GLint font_loc_proj;
    GLint font_loc_sampler;
    GLint font_loc_color;

    // Common GL objects
    GLuint vbo;

    // --- Drawing State ---
    float projection[16];
    float current_color[4];
    float clear_color[4];

    // --- Texture Storage ---
    kvec_t(GLTexture) textures;

    // --- Video Background ---
    GLuint video_texture_id;
    int video_width;
    int video_height;

} GLBackendState;

// Global accessor for the backend state
GLBackendState* geogl_get_state(void);


/* =========================================
 * Platform Abstraction (to be implemented by glfw.c, egl.c, etc.)
 * ========================================= */

// This is the main init function called by the core.
// It's responsible for creating the window and GL context.
int platform_init(uint16_t width, uint16_t height);

void platform_terminate(void);
void platform_swap_buffers(void);
void platform_poll_events(void);
bool platform_should_close(void);
void platform_set_swap_interval(int interval);
double platform_get_time(void);
void* platform_get_proc_address(const char *name);

/* =========================================
 * OpenGL Specifics (opengl.c)
 * ========================================= */

// Initializes shaders, programs, VBOs, etc.
// Requires a valid GL context to be current.
void opengl_init(void);

// Destroys GL objects.
void opengl_terminate(void);


#endif // GEC_BACKEND_GL_INTERNAL_H
