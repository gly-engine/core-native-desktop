#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"
#include <GLFW/glfw3.h>

/* Registers two runtime-selectable backends off this one file: backend:gl_glfw
 * (desktop GL via the native context API) and backend:gles_glfw (GLES2 via
 * GLFW's EGL context path). Which one runs is decided later by the selector in
 * Frontend_Core/update.c; both are registered only if their underlying shared
 * libs are present (ge_lib_available), so an unusable variant never even shows
 * up as a candidate.
 *
 * Neither entry point may exit() on failure — it adds an error and unwinds, so
 * the selector can fall through to the next candidate. */
typedef enum { GE_GLFW_GL, GE_GLFW_GLES } ge_glfw_api_t;

static const struct {
    int key;
    const char *name;
} keymap[] = {
    { GLFW_KEY_UP,           "up" },
    { GLFW_KEY_DOWN,         "down" },
    { GLFW_KEY_LEFT,         "left" },
    { GLFW_KEY_RIGHT,        "right" },
    { GLFW_KEY_Z,            "a" },
    { GLFW_KEY_X,            "b" },
    { GLFW_KEY_C,            "c" },
    { GLFW_KEY_V,            "d" },
    { GLFW_KEY_LEFT_SHIFT,   "menu" },
};

/* GLFW reports the reason asynchronously through the error callback, so stash
 * it here to fold into the backend's error instead of only printing it. */
static char last_glfw_error[128];

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    if (action == GLFW_REPEAT) return;
    gamely_daemon_input_push(key, action != GLFW_RELEASE, 0);
}

static void focus_callback(GLFWwindow* window, int focused) {
    (void)window; (void)focused; /** @todo nao sei o porque disso. */
}

static void glfw_error_callback(int error, const char* description) {
    snprintf(last_glfw_error, sizeof(last_glfw_error), "%s (0x%x)",
             description ? description : "unknown", error);
}

static void glfw_swap_buffers(void) {
    glfwSwapBuffers(geogl_get_state()->window);
}

static double glfw_get_time(void) {
    return glfwGetTime();
}

static void* glfw_proc_address(const char *name) {
    return (void*)glfwGetProcAddress(name);
}

static void glfw_poll_should_close(bool *should_close) {
    *should_close = geogl_get_state()->window && glfwWindowShouldClose(geogl_get_state()->window);
}

static void glfw_terminate(void) {
    glfwTerminate();
}

static const GEWindowOps glfw_ops = {
    .swap_buffers      = glfw_swap_buffers,
    .get_time          = glfw_get_time,
    .get_proc_address  = glfw_proc_address,
    .poll_should_close = glfw_poll_should_close,
    .terminate         = glfw_terminate,
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

static const char *glfw_reason(void) {
    return last_glfw_error[0] ? last_glfw_error : "no reason reported";
}

static void backend_init(gecnd_t *gly, ge_glfw_api_t api) {
    const char *tag = api == GE_GLFW_GLES ? "gles_glfw" : "gl_glfw";
    uint16_t width  = (uint16_t)gly->width;
    uint16_t height = (uint16_t)gly->height;
    GLBackendState *s = geogl_get_state();
    bool glfw_up = false;
    bool ok = false;

    last_glfw_error[0] = '\0';

    do {
        glfwSetErrorCallback(glfw_error_callback);
        if (!glfwInit()) {
            backend_error("[%s] glfwInit failed: %s", tag, glfw_reason());
            break;
        }
        glfw_up = true;

        glfwWindowHint(GLFW_CLIENT_API, api == GE_GLFW_GLES ? GLFW_OPENGL_ES_API : GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, api == GE_GLFW_GLES ? GLFW_EGL_CONTEXT_API : GLFW_NATIVE_CONTEXT_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

        s->window = glfwCreateWindow(
            width, height,
            api == GE_GLFW_GLES ? "gecnd (OpenGL ES/GLFW)" : "gecnd (OpenGL/GLFW)",
            NULL, NULL
        );
        if (!s->window) {
            backend_error("[%s] window creation failed: %s", tag, glfw_reason());
            break;
        }

        glfwMakeContextCurrent(s->window);
        if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
            backend_error("[%s] gladLoadGL failed: could not load GL entry points", tag);
            break;
        }

        glfwSetKeyCallback(s->window, key_callback);
        glfwSetWindowFocusCallback(s->window, focus_callback);
        ge_window_ops_set(&glfw_ops);

        for (uint8_t i = 0; i < sizeof(keymap)/sizeof(*keymap); i++) {
            gamely_daemon_input_add_keycode("glfw", keymap[i].name, keymap[i].key);
        }
        gamely_input_add_cb("@tick", glfwPollEvents, NULL);

        ge_backend_ready(gly, width, height, api == GE_GLFW_GLES);
        ok = true;
    } while (0);

    if (!ok) {
        if (s->window) glfwDestroyWindow(s->window);
        if (glfw_up) glfwTerminate();
        ge_backend_reset(gly);
    }
}

static void backend_init_gl(gecnd_t *gly)   { backend_init(gly, GE_GLFW_GL); }
static void backend_init_gles(gecnd_t *gly) { backend_init(gly, GE_GLFW_GLES); }

__attribute__((constructor))
static void init(void) {
    if (ge_lib_available("libGL.so.1")) {
        gecnd_registry("set", "backend:gl_glfw", (void *)backend_init_gl, NULL);
    }
    bool gles_libs = ge_lib_available("libEGL.so.1") &&
        (ge_lib_available("libGLESv2.so.2") || ge_lib_available("libGLESv2.so"));
    if (gles_libs) {
        gecnd_registry("set", "backend:gles_glfw", (void *)backend_init_gles, NULL);
    }
}
