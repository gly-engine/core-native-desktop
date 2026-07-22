#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <dlfcn.h>

#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"
#include <glad/egl.h>

typedef enum { GE_EGL_GL, GE_EGL_GLES } ge_egl_api_t;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

static void (*glad_gles_loader(const char *name))(void) {
    void (*p)(void) = eglGetProcAddress(name);
    if (p) return p;
    static void *libgl2 = NULL;
    if (libgl2 == NULL) {
        libgl2 = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_GLOBAL);
        if (!libgl2) libgl2 = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!libgl2) libgl2 = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    }
    return libgl2 ? (void (*)(void)) dlsym(libgl2, name) : NULL;
}

static void egl_swap_buffers(void) {
    if (egl_display != EGL_NO_DISPLAY) eglSwapBuffers(egl_display, egl_surface);
}

static double egl_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void* egl_get_proc_address(const char *name) {
    return (void*)eglGetProcAddress(name);
}

static void egl_poll_should_close(bool *should_close) {
    *should_close = false;
}

static void egl_terminate(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
    }
    egl_display = EGL_NO_DISPLAY; egl_context = EGL_NO_CONTEXT; egl_surface = EGL_NO_SURFACE;
    gladLoaderUnloadEGL();
}

static const GEWindowOps egl_ops = {
    .swap_buffers      = egl_swap_buffers,
    .get_time          = egl_get_time,
    .get_proc_address  = egl_get_proc_address,
    .poll_should_close = egl_poll_should_close,
    .terminate         = egl_terminate,
};

static int egl_context_create(ge_egl_api_t api) {
    if (!gladLoaderLoadEGL(EGL_DEFAULT_DISPLAY)) {
        fprintf(stderr, "[egl] gladLoaderLoadEGL failed: could not load EGL symbols\n");
        return -1;
    }

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "[egl] eglGetDisplay failed: no default display (eglError=0x%x)\n", eglGetError());
        return -1;
    }
    EGLint egl_major = 0, egl_minor = 0;
    if (!eglInitialize(egl_display, &egl_major, &egl_minor)) {
        fprintf(stderr, "[egl] eglInitialize failed (eglError=0x%x)\n", eglGetError());
        return -1;
    }
    if (!eglBindAPI(api == GE_EGL_GLES ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) {
        fprintf(stderr, "[egl] eglBindAPI failed: requested API unavailable (eglError=0x%x)\n", eglGetError());
        return -1;
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, api == GE_EGL_GLES ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLConfig config; EGLint num_config = 0;
    if (!eglChooseConfig(egl_display, attribs, &config, 1, &num_config) || num_config < 1) {
        fprintf(stderr, "[egl] eglChooseConfig failed: no matching framebuffer config (num_config=%d, eglError=0x%x)\n", num_config, eglGetError());
        return -1;
    }
    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)0, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[egl] eglCreateWindowSurface failed (eglError=0x%x)\n", eglGetError());
        return -1;
    }

    const EGLint gles_context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT,
                                    api == GE_EGL_GLES ? gles_context_attribs : NULL);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "[egl] eglCreateContext failed: could not create context (eglError=0x%x)\n", eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "[egl] eglMakeCurrent failed (eglError=0x%x)\n", eglGetError());
        return -1;
    }

    eglSwapInterval(egl_display, 0);
    return 0;
}

static void core_pre_init(const char *key, void *value, void *usr) {
    (void)key;
    ge_egl_api_t api = (ge_egl_api_t)(intptr_t)usr;
    gecnd_t *gly = (gecnd_t *)value;
    uint16_t width  = (uint16_t)gly->width;
    uint16_t height = (uint16_t)gly->height;

    if (egl_context_create(api) != 0) {
        fprintf(stderr, "[egl] platform_init failed: unable to bring up EGL context (%ux%u)\n", width, height);
        exit(1);
    }
    if (!gladLoadGL((GLADloadfunc)glad_gles_loader)) {
        fprintf(stderr, "[egl] gladLoadGL failed: could not load GL/GLES symbols\n");
        exit(1);
    }

    ge_window_ops_set(&egl_ops);
    ge_backend_ready(gly, width, height, api == GE_EGL_GLES);
}

__attribute__((constructor))
static void init(void) {
    if (!ge_lib_available("libEGL.so.1") && !ge_lib_available("libEGL.so")) return;

    if (ge_lib_available("libGL.so.1")) {
        gecnd_registry("set",  "backend:gl_egl", NULL, NULL);
        gecnd_registry("hook", "backend:gl_egl:pre_init", (void *)core_pre_init, (void *)(intptr_t)GE_EGL_GL);
    }
    if (ge_lib_available("libGLESv2.so.2") || ge_lib_available("libGLESv2.so")) {
        gecnd_registry("set",  "backend:gles_egl", NULL, NULL);
        gecnd_registry("hook", "backend:gles_egl:pre_init", (void *)core_pre_init, (void *)(intptr_t)GE_EGL_GLES);
    }
}
