#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <dlfcn.h>

#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"
#include <glad/egl.h>

/* Registers backend:gl_egl (desktop GL on an EGL context) and
 * backend:gles_egl (GLES2 on an EGL context). Selection happens at runtime in
 * Frontend_Core/update.c, so neither entry point may exit() on failure: it
 * adds an error and fully tears the partial context down, because the selector
 * will try the next candidate right after in the same process. */
typedef enum { GE_EGL_GL, GE_EGL_GLES } ge_egl_api_t;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

typedef EGLBoolean (EGLAPIENTRYP PFNEGLSWAPINTERVALPROC)(EGLDisplay dpy, EGLint interval);

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
}

static const GEWindowOps egl_ops = {
    .swap_buffers      = egl_swap_buffers,
    .get_time          = egl_get_time,
    .get_proc_address  = egl_get_proc_address,
    .poll_should_close = egl_poll_should_close,
    .terminate         = egl_terminate,
};

/* Returns NULL on success, or a static reason string. On failure the caller
 * must still call egl_terminate() to drop whatever did get created. */
static const char *egl_context_create(ge_egl_api_t api) {
    if (!gladLoaderLoadEGL(EGL_NO_DISPLAY)) return "could not load EGL symbols";

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) return "no default display";

    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_display, &major, &minor)) return "eglInitialize failed";

    /* Reload now that we have a display: this picks up the display's client
     * extensions, which the bootstrap load above cannot see. */
    gladLoaderLoadEGL(egl_display);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, api == GE_EGL_GLES ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLConfig config; EGLint num_config = 0;
    if (!eglChooseConfig(egl_display, attribs, &config, 1, &num_config) || num_config < 1)
        return "no matching framebuffer config";

    if (!eglBindAPI(api == GE_EGL_GLES ? EGL_OPENGL_ES_API : EGL_OPENGL_API))
        return "eglBindAPI failed";

    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)0, NULL);
    if (egl_surface == EGL_NO_SURFACE) return "eglCreateWindowSurface failed";

    const EGLint gles_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT,
                                   api == GE_EGL_GLES ? gles_attribs : NULL);
    if (egl_context == EGL_NO_CONTEXT) return "eglCreateContext failed";

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context))
        return "eglMakeCurrent failed";

    PFNEGLSWAPINTERVALPROC swap_interval = (PFNEGLSWAPINTERVALPROC)eglGetProcAddress("eglSwapInterval");
    if (swap_interval) swap_interval(egl_display, 0);

    return NULL;
}

/* Records why this candidate is out. The selector (Frontend_Core/backend.c)
 * hooks error:backend, so setting it is what marks the attempt as failed. */
static void backend_error(const char *fmt, ...) {
    char msg[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    gecnd_registry("set", "error:backend", msg, "strdup=val");
}

static void backend_init(gecnd_t *gly, ge_egl_api_t api) {
    const char *tag = api == GE_EGL_GLES ? "gles_egl" : "gl_egl";
    uint16_t width  = (uint16_t)gly->width;
    uint16_t height = (uint16_t)gly->height;
    bool ok = false;

    do {
        const char *why = egl_context_create(api);
        if (why) {
            backend_error("[%s] %s (eglError=0x%x)", tag, why, eglGetError());
            break;
        }
        if (!gladLoadGL((GLADloadfunc)glad_gles_loader)) {
            backend_error("[%s] gladLoadGL failed: could not load GL/GLES entry points", tag);
            break;
        }

        ge_window_ops_set(&egl_ops);
        ge_backend_ready(gly, width, height, api == GE_EGL_GLES);
        ok = true;
    } while (0);

    if (!ok) {
        egl_terminate();   /* guards each handle, so it covers every partial state */
        ge_backend_reset(gly);
    }
}

static void backend_init_gl(gecnd_t *gly)   { backend_init(gly, GE_EGL_GL); }
static void backend_init_gles(gecnd_t *gly) { backend_init(gly, GE_EGL_GLES); }

__attribute__((constructor))
static void init(void) {
    if (!ge_lib_available("libEGL.so.1") && !ge_lib_available("libEGL.so")) return;

    if (ge_lib_available("libGL.so.1")) {
        gecnd_registry("set", "backend:gl_egl", (void *)backend_init_gl, NULL);
    }
    if (ge_lib_available("libGLESv2.so.2") || ge_lib_available("libGLESv2.so")) {
        gecnd_registry("set", "backend:gles_egl", (void *)backend_init_gles, NULL);
    }
}
