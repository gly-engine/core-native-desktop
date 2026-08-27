#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"
#include <SDL.h>

typedef enum { GE_SDL_GL, GE_SDL_GLES } ge_sdl_api_t;

#define SDL_FOREACH_SYM(X) \
    X(SDL_Init) \
    X(SDL_Quit) \
    X(SDL_GetError) \
    X(SDL_CreateWindow) \
    X(SDL_DestroyWindow) \
    X(SDL_GL_SetAttribute) \
    X(SDL_GL_CreateContext) \
    X(SDL_GL_MakeCurrent) \
    X(SDL_GL_DeleteContext) \
    X(SDL_GL_SwapWindow) \
    X(SDL_GL_GetProcAddress) \
    X(SDL_GL_SetSwapInterval) \
    X(SDL_PollEvent) \
    X(SDL_GetTicks64)

#define SDL_DECLARE_PTR(name) static typeof(name) *p_##name;
SDL_FOREACH_SYM(SDL_DECLARE_PTR)
#undef SDL_DECLARE_PTR

static void *sdl_handle = NULL;
static SDL_Window *sdl_window = NULL;
static SDL_GLContext sdl_gl_context = NULL;
static bool sdl_should_close = false;

static const struct {
    int key;
    const char *name;
} keymap[] = {
    { SDLK_UP,     "up" },
    { SDLK_DOWN,   "down" },
    { SDLK_LEFT,   "left" },
    { SDLK_RIGHT,  "right" },
    { SDLK_z,      "a" },
    { SDLK_x,      "b" },
    { SDLK_c,      "c" },
    { SDLK_v,      "d" },
    { SDLK_LSHIFT, "menu" },
};

static void *sdl_dlopen(void) {
    static const char *candidates[] = {
        "libSDL2-2.0.so.0", "libSDL2-2.0.so", "libSDL2.so",
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(*candidates); i++) {
        void *h = dlopen(candidates[i], RTLD_LAZY | RTLD_GLOBAL);
        if (h) return h;
    }
    return NULL;
}

static bool sdl_load_symbols(void *h, const char **missing) {
#define SDL_RESOLVE(name) \
    p_##name = (typeof(p_##name))dlsym(h, #name); \
    if (!p_##name) { *missing = #name; return false; }
    SDL_FOREACH_SYM(SDL_RESOLVE)
#undef SDL_RESOLVE
    return true;
}

static void sdl_pump_events(void) {
    SDL_Event ev;
    while (p_SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                sdl_should_close = true;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (ev.key.repeat) break;
                gamely_daemon_input_push((uint32_t)ev.key.keysym.sym, ev.type == SDL_KEYDOWN, 0);
                break;
            default:
                break;
        }
    }
}

static void sdl_swap_buffers(void) {
    p_SDL_GL_SwapWindow(sdl_window);
}

static double sdl_get_time(void) {
    return (double)p_SDL_GetTicks64() / 1000.0;
}

static void *sdl_proc_address(const char *name) {
    return p_SDL_GL_GetProcAddress(name);
}

static void sdl_poll_should_close(bool *should_close) {
    *should_close = sdl_should_close;
}

static void sdl_terminate(void) {
    if (sdl_gl_context) p_SDL_GL_DeleteContext(sdl_gl_context);
    if (sdl_window) p_SDL_DestroyWindow(sdl_window);
    if (p_SDL_Quit) p_SDL_Quit();
    if (sdl_handle) dlclose(sdl_handle);
    sdl_gl_context = NULL;
    sdl_window = NULL;
    sdl_handle = NULL;
}

static const GEWindowOps sdl_ops = {
    .swap_buffers      = sdl_swap_buffers,
    .get_time          = sdl_get_time,
    .get_proc_address  = sdl_proc_address,
    .poll_should_close = sdl_poll_should_close,
    .terminate         = sdl_terminate,
};

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

static void backend_init(gecnd_t *gly, ge_sdl_api_t api) {
    const char *tag = api == GE_SDL_GLES ? "gles_sdl2" : "gl_sdl2";
    uint16_t width  = (uint16_t)gly->width;
    uint16_t height = (uint16_t)gly->height;
    GLBackendState *s = geogl_get_state();
    bool sdl_inited = false;
    bool ok = false;

    sdl_should_close = false;

    do {
        if (api == GE_SDL_GL && !ge_lib_available("libGL.so.1")) {
            backend_error("[%s] libGL.so.1 not available", tag);
            break;
        }
        if (api == GE_SDL_GLES &&
            (!ge_lib_available("libEGL.so.1") && !ge_lib_available("libEGL.so"))) {
            backend_error("[%s] libEGL not available", tag);
            break;
        }

        sdl_handle = sdl_dlopen();
        if (!sdl_handle) {
            backend_error("[%s] libSDL2 not available", tag);
            break;
        }

        const char *missing = NULL;
        if (!sdl_load_symbols(sdl_handle, &missing)) {
            backend_error("[%s] libSDL2 missing symbol %s", tag, missing);
            break;
        }

        if (p_SDL_Init(SDL_INIT_VIDEO) != 0) {
            backend_error("[%s] SDL_Init failed: %s", tag, p_SDL_GetError());
            break;
        }
        sdl_inited = true;

        p_SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        p_SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        p_SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
            api == GE_SDL_GLES ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

        sdl_window = p_SDL_CreateWindow(
            api == GE_SDL_GLES ? "gecnd (OpenGL ES/SDL2)" : "gecnd (OpenGL/SDL2)",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height, SDL_WINDOW_OPENGL
        );
        if (!sdl_window) {
            backend_error("[%s] window creation failed: %s", tag, p_SDL_GetError());
            break;
        }

        sdl_gl_context = p_SDL_GL_CreateContext(sdl_window);
        if (!sdl_gl_context) {
            backend_error("[%s] SDL_GL_CreateContext failed: %s", tag, p_SDL_GetError());
            break;
        }

        p_SDL_GL_MakeCurrent(sdl_window, sdl_gl_context);
        if (!gladLoadGL((GLADloadfunc)sdl_proc_address)) {
            backend_error("[%s] gladLoadGL failed: could not load GL entry points", tag);
            break;
        }

        p_SDL_GL_SetSwapInterval(1);
        ge_window_ops_set(&sdl_ops);
        s->window = sdl_window;

        for (uint8_t i = 0; i < sizeof(keymap)/sizeof(*keymap); i++) {
            gamely_daemon_input_add_keycode("sdl2", keymap[i].name, keymap[i].key);
        }
        gamely_input_add_cb("@tick", sdl_pump_events, NULL);

        ge_backend_ready(gly, width, height, api == GE_SDL_GLES);

        static const bool enabled = true;
        gecnd_registry("set", "internal:gl", &enabled, NULL);

        ok = true;
    } while (0);

    if (!ok) {
        sdl_terminate();
        (void)sdl_inited;
        ge_backend_reset(gly);
    }
}

static void backend_init_gl(gecnd_t *gly)   { backend_init(gly, GE_SDL_GL); }
static void backend_init_gles(gecnd_t *gly) { backend_init(gly, GE_SDL_GLES); }

__attribute__((constructor))
static void init(void) {
    gecnd_registry("set", "backend:gl_sdl2",   (void *)backend_init_gl,   NULL);
    gecnd_registry("set", "backend:gles_sdl2", (void *)backend_init_gles, NULL);
}
