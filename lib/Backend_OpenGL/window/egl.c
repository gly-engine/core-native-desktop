#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <dlfcn.h>

#include "gecnd.h"
#include "gebuffer.h"
#include "gehook.h"
#include "geopengl.h"

static GLBackendState g_gl_state;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

GLBackendState* geogl_get_state(void) {
    return &g_gl_state;
}

static void (*glad_gles2_loader(const char *name))(void) {
    void (*p)(void) = eglGetProcAddress(name);
    if (p) return p;
    static void* libgles = NULL;
    if (libgles == NULL) {
        libgles = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_GLOBAL);
        if (libgles == NULL) libgles = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (libgles != NULL) return (void (*)(void)) dlsym(libgles, name);
    return NULL;
}

typedef EGLBoolean (EGLAPIENTRYP PFNEGLSWAPINTERVALPROC)(EGLDisplay dpy, EGLint interval);

int platform_init(uint16_t width, uint16_t height) {
    if (!gladLoaderLoadEGL(EGL_DEFAULT_DISPLAY)) return -1;
    GLBackendState *state = geogl_get_state();
    state->window_width = width; state->window_height = height;
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) return -1;
    if (!eglInitialize(egl_display, NULL, NULL)) return -1;
    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLConfig config; EGLint num_config;
    if (!eglChooseConfig(egl_display, attribs, &config, 1, &num_config)) return -1;
    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)0, NULL);
    const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) return -1;
    
    PFNEGLSWAPINTERVALPROC eglSwapIntervalPtr = (PFNEGLSWAPINTERVALPROC)eglGetProcAddress("eglSwapInterval");
    if (eglSwapIntervalPtr) eglSwapIntervalPtr(egl_display, 0);
    
    return 0;
}

void platform_terminate(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
    }
    egl_display = EGL_NO_DISPLAY; egl_context = EGL_NO_CONTEXT; egl_surface = EGL_NO_SURFACE;
}

void platform_swap_buffers(void) {
    if (egl_display != EGL_NO_DISPLAY) eglSwapBuffers(egl_display, egl_surface);
}

double platform_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void gly_hook_display_init(uint16_t width, uint16_t height) {
    GLBackendState *s = geogl_get_state();
    if (platform_init(width, height) != 0) exit(1);
    if (!gladLoadGLES2((GLADloadfunc)glad_gles2_loader)) exit(1);
    kv_init(s->textures);
    init_all_shaders(true);
    ge_pipeline_init(width, height);
    glGenBuffers(1, &s->video_vbo);
    glGenBuffers(1, &s->post_vbo);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    native_draw_color(0xFFFFFFFF);
    native_draw_clear(0x1A2B3CFF);
    mat4_ortho(s->projection, 0, width, height, 0, -1, 1);
    s->last_frame_time = platform_get_time();
}

void gly_hook_display_dt(int16_t *delta_time) {
    GLBackendState *state = geogl_get_state();
    double t = platform_get_time();
    *delta_time = (int16_t)((t - state->last_frame_time) * 1000.0);
    state->last_frame_time = t;
}

void gly_hook_should_close(bool *should_close) {
    *should_close = false;
}

void gly_hook_display_close(void) {
    GLBackendState *s = geogl_get_state();
    terminate_all_shaders();
    if (s->vbos[0]) glDeleteBuffers(3, s->vbos);
    glDeleteBuffers(1, &s->video_vbo);
    glDeleteBuffers(1, &s->post_vbo);
    if (s->post_fbo) glDeleteFramebuffers(1, &s->post_fbo);
    if (s->post_fbo_texture) glDeleteTextures(1, &s->post_fbo_texture);
    if (s->video_textures[0]) glDeleteTextures(3, s->video_textures);
    if (s->atlas_id) glDeleteTextures(1, &s->atlas_id);
    kv_destroy(s->textures);
    gecnd_buffer_free();
    native_text_terminate();
    if (s->batch_buffer) free(s->batch_buffer);
    platform_terminate();
}
