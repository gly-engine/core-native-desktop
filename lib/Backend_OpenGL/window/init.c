#include <stdio.h>
#include <stdlib.h>

#include "geopengl.h"
/* =====================
   Global state
   ===================== */
static GLBackendState g_gl_state;

GLBackendState* geogl_get_state(void) {
    return &g_gl_state;
}

/* =====================
   Color Conversion
   ===================== */

/* =====================
   Hooks
   ===================== */
void gly_hook_display_init(uint16_t width, uint16_t height) {
    // Platform init (e.g., GLFW)
    if (platform_init(width, height) != 0) {
        // platform_init should die on failure, but as a safeguard:
        fprintf(stderr, "[FATAL] Platform initialization failed.\n");
        exit(1);
    }

    // Generic OpenGL init (shaders, etc.)
    opengl_init();

    // Set initial state
    native_draw_color(0xFFFFFFFF); // White
    native_draw_clear(0x1A2B3CFF); // Dark blue
    
    g_gl_state.last_frame_time = platform_get_time();
}

void gly_hook_display_fps(uint8_t fps) {
    platform_set_swap_interval(fps == 0 ? 0 : 1);
}

void gly_hook_display_dt(int16_t *delta_time) {
    double t = platform_get_time();
    *delta_time = (int16_t)((t - g_gl_state.last_frame_time) * 1000.0);
    g_gl_state.last_frame_time = t;
}

void gly_hook_should_close(bool *should_close) {
    *should_close = platform_should_close();
}

void gly_hook_display_close(void) {
    opengl_terminate();
    platform_terminate();
}
