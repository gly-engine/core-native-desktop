#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>

#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"

/* Single GL backend state + window-ops dispatch shared by every window/*.c
 * backend (glfw.c, egl.c, ...). All of them are always linked in now that the
 * window system is a runtime choice, so the state, the gly_hook_* display
 * hooks and the platform_* glue can only be defined once — only the backend
 * that won selection (see Frontend_Core/update.c) is ever wired into ops. */
static GLBackendState g_gl_state;

GLBackendState* geogl_get_state(void) {
    return &g_gl_state;
}

void ge_window_ops_set(const GEWindowOps *ops) {
    g_gl_state.ops = *ops;
}

bool ge_lib_available(const char *soname) {
    void *h = dlopen(soname, RTLD_LAZY);
    if (!h) return false;
    dlclose(h);
    return true;
}

void platform_swap_buffers(void) {
    if (g_gl_state.ops.swap_buffers) g_gl_state.ops.swap_buffers();
}

double platform_get_time(void) {
    return g_gl_state.ops.get_time ? g_gl_state.ops.get_time() : 0.0;
}

void* platform_get_proc_address(const char *name) {
    return g_gl_state.ops.get_proc_address ? g_gl_state.ops.get_proc_address(name) : NULL;
}

void ge_backend_ready(gecnd_t *gly, uint16_t width, uint16_t height, bool is_gles) {
    GLBackendState *s = &g_gl_state;
    s->is_gles = is_gles;
    s->window_width = width;
    s->window_height = height;

    kv_init(s->textures);
    init_all_shaders(is_gles);
    ge_pipeline_init(width, height);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);
    mat4_ortho(s->projection, 0, width, height, 0, -(float)GE_MAX_LAYERS, (float)GE_MAX_LAYERS);
    s->last_frame_time = platform_get_time();
    gly->internal |= GECND_INTERNAL_HW_GL_READY;
    ge_hw_register();
}

void ge_backend_reset(gecnd_t *gly) {
    g_gl_state.ops = (GEWindowOps){0};
    g_gl_state.window = NULL;
    if (gly) gly->internal &= ~GECND_INTERNAL_HW_GL_READY;
}

void gly_hook_display_dt(int16_t *delta_time) {
    double t = platform_get_time();
    *delta_time = (int16_t)((t - g_gl_state.last_frame_time) * 1000.0);
    g_gl_state.last_frame_time = t;
}

void gly_hook_should_close(bool *should_close) {
    if (g_gl_state.ops.poll_should_close) g_gl_state.ops.poll_should_close(should_close);
}

void gly_hook_display_close(void) {
    terminate_all_shaders();
    ge_pipeline_terminate();
    gamely_daemon_media_shutdown();
    native_text_terminate();
    if (g_gl_state.ops.terminate) g_gl_state.ops.terminate();
}
