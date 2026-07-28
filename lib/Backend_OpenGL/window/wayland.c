#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>

#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"

#include <wayland-util.h>
#include <wayland-client-core.h>
#include <wayland-egl-core.h>
#include <glad/egl.h>

struct wl_registry;
struct wl_compositor;

enum {
    WL_DISPLAY_GET_REGISTRY      = 1,
    WL_REGISTRY_BIND             = 0,
    WL_COMPOSITOR_CREATE_SURFACE = 0,
    WL_SURFACE_DESTROY           = 0,
    WL_SURFACE_COMMIT            = 6,
};

typedef enum { GE_WL_GL, GE_WL_GLES } ge_wl_api_t;

static const char *const s_wl_client_names[] = { "libwayland-client.so.0", "libwayland-client.so", NULL };
static const char *const s_wl_egl_names[]    = { "libwayland-egl.so.1", "libwayland-egl.so.0", "libwayland-egl.so", NULL };
static const char *const s_egl_names[]       = { "libEGL.so.1", "libEGL.so", NULL };
static const char *const s_gl_names[]        = { "libGL.so.1", NULL };
static const char *const s_gles_names[]      = { "libGLESv2.so.2", "libGLESv2.so", NULL };

static void *dlopen_any(const char *const *names) {
    for (; *names; names++) {
        void *h = dlopen(*names, RTLD_LAZY | RTLD_GLOBAL);
        if (h) return h;
    }
    return NULL;
}

static bool ge_lib_available_any(const char *const *names) {
    for (; *names; names++) {
        if (ge_lib_available(*names)) return true;
    }
    return false;
}

static void                                *s_libwl_client;
static void                                *s_libwl_egl;
static typeof(wl_display_connect)          *p_wl_display_connect;
static typeof(wl_display_disconnect)       *p_wl_display_disconnect;
static typeof(wl_display_dispatch_pending) *p_wl_display_dispatch_pending;
static typeof(wl_display_roundtrip)        *p_wl_display_roundtrip;
static typeof(wl_proxy_marshal_flags)      *p_wl_proxy_marshal_flags;
static typeof(wl_proxy_add_listener)       *p_wl_proxy_add_listener;
static typeof(wl_proxy_destroy)            *p_wl_proxy_destroy;
static typeof(wl_proxy_get_version)        *p_wl_proxy_get_version;
static typeof(wl_egl_window_create)        *p_wl_egl_window_create;
static typeof(wl_egl_window_destroy)       *p_wl_egl_window_destroy;
static const struct wl_interface           *p_wl_registry_interface;
static const struct wl_interface           *p_wl_compositor_interface;
static const struct wl_interface           *p_wl_surface_interface;

typedef struct {
    struct wl_display    *display;
    struct wl_registry   *registry;
    struct wl_compositor *compositor;
    struct wl_surface    *surface;
    struct wl_egl_window *egl_window;
} ge_wl_state_t;

static ge_wl_state_t s_wl;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

static bool wl_load_libs(void) {
    if (s_libwl_client && s_libwl_egl) return true;
    s_libwl_client = dlopen_any(s_wl_client_names);
    s_libwl_egl    = dlopen_any(s_wl_egl_names);
    if (!s_libwl_client || !s_libwl_egl) return false;

#define LOAD_CLIENT(sym) (p_##sym = (typeof(sym)*)dlsym(s_libwl_client, #sym))
    if (!LOAD_CLIENT(wl_display_connect))          return false;
    if (!LOAD_CLIENT(wl_display_disconnect))       return false;
    if (!LOAD_CLIENT(wl_display_dispatch_pending)) return false;
    if (!LOAD_CLIENT(wl_display_roundtrip))        return false;
    if (!LOAD_CLIENT(wl_proxy_marshal_flags))      return false;
    if (!LOAD_CLIENT(wl_proxy_add_listener))       return false;
    if (!LOAD_CLIENT(wl_proxy_destroy))            return false;
    if (!LOAD_CLIENT(wl_proxy_get_version))        return false;
#undef LOAD_CLIENT

    p_wl_registry_interface   = dlsym(s_libwl_client, "wl_registry_interface");
    p_wl_compositor_interface = dlsym(s_libwl_client, "wl_compositor_interface");
    p_wl_surface_interface    = dlsym(s_libwl_client, "wl_surface_interface");
    if (!p_wl_registry_interface || !p_wl_compositor_interface || !p_wl_surface_interface) return false;

    p_wl_egl_window_create  = (typeof(wl_egl_window_create)*)  dlsym(s_libwl_egl, "wl_egl_window_create");
    p_wl_egl_window_destroy = (typeof(wl_egl_window_destroy)*) dlsym(s_libwl_egl, "wl_egl_window_destroy");
    return p_wl_egl_window_create && p_wl_egl_window_destroy;
}

static struct wl_registry *wl_display_get_registry_(struct wl_display *display) {
    return (struct wl_registry *) p_wl_proxy_marshal_flags(
        (struct wl_proxy *) display, WL_DISPLAY_GET_REGISTRY, p_wl_registry_interface,
        p_wl_proxy_get_version((struct wl_proxy *) display), 0, NULL);
}

static void *wl_registry_bind_(struct wl_registry *registry, uint32_t name, const struct wl_interface *iface, uint32_t version) {
    return (void *) p_wl_proxy_marshal_flags(
        (struct wl_proxy *) registry, WL_REGISTRY_BIND, iface, version, 0,
        name, iface->name, version, NULL);
}

static struct wl_surface *wl_compositor_create_surface_(struct wl_compositor *compositor) {
    return (struct wl_surface *) p_wl_proxy_marshal_flags(
        (struct wl_proxy *) compositor, WL_COMPOSITOR_CREATE_SURFACE, p_wl_surface_interface,
        p_wl_proxy_get_version((struct wl_proxy *) compositor), 0, NULL);
}

static void wl_surface_commit_(struct wl_surface *surface) {
    p_wl_proxy_marshal_flags((struct wl_proxy *) surface, WL_SURFACE_COMMIT, NULL,
        p_wl_proxy_get_version((struct wl_proxy *) surface), 0);
}

static void wl_surface_destroy_(struct wl_surface *surface) {
    p_wl_proxy_marshal_flags((struct wl_proxy *) surface, WL_SURFACE_DESTROY, NULL,
        p_wl_proxy_get_version((struct wl_proxy *) surface), WL_MARSHAL_FLAG_DESTROY);
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (!s_wl.compositor && strcmp(interface, "wl_compositor") == 0) {
        s_wl.compositor = (struct wl_compositor *) wl_registry_bind_(registry, name, p_wl_compositor_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct {
    void (*global)(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    void (*global_remove)(void *data, struct wl_registry *registry, uint32_t name);
} registry_listener = {
    registry_global,
    registry_global_remove,
};

static int wl_connect_and_bind(void) {
    if (!wl_load_libs()) return -1;
    s_wl.display = p_wl_display_connect(NULL);
    if (!s_wl.display) return -1;
    s_wl.registry = wl_display_get_registry_(s_wl.display);
    if (!s_wl.registry) return -1;
    p_wl_proxy_add_listener((struct wl_proxy *) s_wl.registry, (void (**)(void)) &registry_listener, NULL);
    p_wl_display_roundtrip(s_wl.display);
    if (!s_wl.compositor) return -1;
    s_wl.surface = wl_compositor_create_surface_(s_wl.compositor);
    return s_wl.surface ? 0 : -1;
}

static void (*wayland_gles_loader(const char *name))(void) {
    void (*p)(void) = (void (*)(void)) eglGetProcAddress(name);
    if (p) return p;
    static void *libgl2 = NULL;
    if (libgl2 == NULL) {
        libgl2 = dlopen_any(s_gles_names);
        if (!libgl2) libgl2 = dlopen_any(s_gl_names);
    }
    return libgl2 ? (void (*)(void)) dlsym(libgl2, name) : NULL;
}

static int wayland_egl_create(ge_wl_api_t api, uint16_t width, uint16_t height) {
    if (!gladLoaderLoadEGL(EGL_NO_DISPLAY)) {
        fprintf(stderr, "[wayland] gladLoaderLoadEGL failed: could not load EGL symbols\n");
        return -1;
    }

    const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (client_ext && (strstr(client_ext, "EGL_EXT_platform_wayland") || strstr(client_ext, "EGL_KHR_platform_wayland"))) {
        egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_EXT, s_wl.display, NULL);
    } else {
        egl_display = eglGetDisplay((EGLNativeDisplayType) s_wl.display);
    }
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "[wayland] eglGetDisplay failed: no display (eglError=0x%x)\n", eglGetError());
        return -1;
    }
    EGLint egl_major = 0, egl_minor = 0;
    if (!eglInitialize(egl_display, &egl_major, &egl_minor)) {
        fprintf(stderr, "[wayland] eglInitialize failed (eglError=0x%x)\n", eglGetError());
        return -1;
    }
    if (!eglBindAPI(api == GE_WL_GLES ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) {
        fprintf(stderr, "[wayland] eglBindAPI failed: requested API unavailable (eglError=0x%x)\n", eglGetError());
        return -1;
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, api == GE_WL_GLES ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLConfig config; EGLint num_config = 0;
    if (!eglChooseConfig(egl_display, attribs, &config, 1, &num_config) || num_config < 1) {
        fprintf(stderr, "[wayland] eglChooseConfig failed: no matching framebuffer config (num_config=%d, eglError=0x%x)\n", num_config, eglGetError());
        return -1;
    }

    s_wl.egl_window = p_wl_egl_window_create(s_wl.surface, width, height);
    if (!s_wl.egl_window) {
        fprintf(stderr, "[wayland] wl_egl_window_create failed\n");
        return -1;
    }

    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType) s_wl.egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[wayland] eglCreateWindowSurface failed (eglError=0x%x)\n", eglGetError());
        return -1;
    }

    const EGLint gles_context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT,
                                    api == GE_WL_GLES ? gles_context_attribs : NULL);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "[wayland] eglCreateContext failed: could not create context (eglError=0x%x)\n", eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "[wayland] eglMakeCurrent failed (eglError=0x%x)\n", eglGetError());
        return -1;
    }

    wl_surface_commit_(s_wl.surface);
    p_wl_display_roundtrip(s_wl.display);
    eglSwapInterval(egl_display, 0);
    return 0;
}

static void wayland_swap_buffers(void) {
    if (egl_display != EGL_NO_DISPLAY) eglSwapBuffers(egl_display, egl_surface);
}

static double wayland_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void* wayland_get_proc_address(const char *name) {
    return (void*)eglGetProcAddress(name);
}

static void wayland_poll_should_close(bool *should_close) {
    if (s_wl.display) p_wl_display_dispatch_pending(s_wl.display);
    *should_close = false;
}

static void wayland_dispatch_pending(void) {
    if (s_wl.display) p_wl_display_dispatch_pending(s_wl.display);
}

static void wayland_terminate(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
    }
    if (s_wl.egl_window) p_wl_egl_window_destroy(s_wl.egl_window);
    if (s_wl.surface)    wl_surface_destroy_(s_wl.surface);
    if (s_wl.registry)   p_wl_proxy_destroy((struct wl_proxy *) s_wl.registry);
    if (s_wl.display)    p_wl_display_disconnect(s_wl.display);
    gladLoaderUnloadEGL();
    memset(&s_wl, 0, sizeof(s_wl));
    egl_display = EGL_NO_DISPLAY; egl_context = EGL_NO_CONTEXT; egl_surface = EGL_NO_SURFACE;
}

static const GEWindowOps wayland_ops = {
    .swap_buffers      = wayland_swap_buffers,
    .get_time          = wayland_get_time,
    .get_proc_address  = wayland_get_proc_address,
    .poll_should_close = wayland_poll_should_close,
    .terminate         = wayland_terminate,
};

static void core_pre_init(const char *key, void *value, void *usr) {
    (void)key;
    ge_wl_api_t api = (ge_wl_api_t)(intptr_t)usr;
    gecnd_t *gly = (gecnd_t *)value;
    uint16_t width  = (uint16_t)gly->width;
    uint16_t height = (uint16_t)gly->height;

    if (wl_connect_and_bind() != 0) {
        fprintf(stderr, "[wayland] could not connect to compositor / bind wl_compositor\n");
        exit(1);
    }
    if (wayland_egl_create(api, width, height) != 0) {
        fprintf(stderr, "[wayland] platform_init failed: unable to bring up EGL context (%ux%u)\n", width, height);
        exit(1);
    }
    if (!gladLoadGL((GLADloadfunc)wayland_gles_loader)) {
        fprintf(stderr, "[wayland] gladLoadGL failed: could not load GL/GLES symbols\n");
        exit(1);
    }

    ge_window_ops_set(&wayland_ops);
    gamely_input_add_cb("@tick", wayland_dispatch_pending, NULL);
    ge_backend_ready(gly, width, height, api == GE_WL_GLES);
}

__attribute__((constructor))
static void init(void) {
    if (!ge_lib_available_any(s_wl_client_names) || !ge_lib_available_any(s_wl_egl_names)) return;
    if (!ge_lib_available_any(s_egl_names)) return;

    if (ge_lib_available_any(s_gl_names)) {
        gecnd_registry("set",  "backend:gl_wayland", NULL, NULL);
        gecnd_registry("hook", "backend:gl_wayland:pre_init", (void *)core_pre_init, (void *)(intptr_t)GE_WL_GL);
    }
    if (ge_lib_available_any(s_gles_names)) {
        gecnd_registry("set",  "backend:gles_wayland", NULL, NULL);
        gecnd_registry("hook", "backend:gles_wayland:pre_init", (void *)core_pre_init, (void *)(intptr_t)GE_WL_GLES);
    }
}
