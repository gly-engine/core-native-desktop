#ifndef GEC_BACKEND_GL_INTERNAL_H
#define GEC_BACKEND_GL_INTERNAL_H

#include <glad/gl.h>
#include <stdint.h>
#include <stdbool.h>

// Forward declaration for GLFW window
struct GLFWwindow;

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

    // Common GL objects
    GLuint vbo;

    // --- Drawing State ---
    float current_color[4];
    float clear_color[4];

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


/* =========================================
 * Native Drawing API (various files)
 * ========================================= */
void native_draw_start(void);
void native_draw_flush(void);
void native_draw_color(uint32_t color);
void native_draw_clear(uint32_t color);
void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r);
void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2);

#endif // GEC_BACKEND_GL_INTERNAL_H
