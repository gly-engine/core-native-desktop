#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"
#include <GLFW/glfw3.h>

/* Registers two runtime-selectable backends off this one file: backend:gl_glfw
 * (desktop GL via GLX) and backend:gles_glfw (GLES2 via GLFW's EGL context
 * path). Which one actually runs is picked later by Frontend_Core/update.c;
 * both are only registered here if their underlying shared libs are present
 * (see ge_lib_available), so an unavailable variant never shows up as a
 * candidate. */
typedef enum { GE_GLFW_GL, GE_GLFW_GLES } ge_glfw_api_t;

static void die(const char *msg) {
    fprintf(stderr, "[FATAL] %s\n", msg);
    exit(1);
}

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

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    if (action == GLFW_REPEAT) return;
    gamely_daemon_input_push(key, action != GLFW_RELEASE, 0);
}

static void focus_callback(GLFWwindow* window, int focused) {
    (void)window; (void)focused; /** @todo nao sei o porque disso. */
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
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

static int glfw_window_create(uint16_t width, uint16_t height, ge_glfw_api_t api) {
    GLBackendState *state = geogl_get_state();
    state->window_width = width;
    state->window_height = height;

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, api == GE_GLFW_GLES ? GLFW_OPENGL_ES_API : GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, api == GE_GLFW_GLES ? GLFW_EGL_CONTEXT_API : GLFW_NATIVE_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    state->window = glfwCreateWindow(
        width, height,
        api == GE_GLFW_GLES ? "gecnd (OpenGL ES/GLFW)" : "gecnd (OpenGL/GLFW)",
        NULL, NULL
    );
    if (!state->window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(state->window);
    glfwSetKeyCallback(state->window, key_callback);
    glfwSetWindowFocusCallback(state->window, focus_callback);
    return 0;
}

static void core_pre_init(const char *key, void *value, void *usr) {
    (void)key;
    ge_glfw_api_t api = (ge_glfw_api_t)(intptr_t)usr;
    gecnd_t *gly = (gecnd_t *)value;
    uint16_t width  = (uint16_t)gly->width;
    uint16_t height = (uint16_t)gly->height;

    if (glfw_window_create(width, height, api) != 0) die("GLFW window/context creation failed");
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) die("GLAD load failed");

    ge_window_ops_set(&glfw_ops);

    for (uint8_t i = 0; i < sizeof(keymap)/sizeof(*keymap); i++) {
        gamely_daemon_input_add_keycode("glfw", keymap[i].name, keymap[i].key);
    }
    gamely_input_add_cb("@tick", glfwPollEvents, NULL);

    ge_backend_ready(gly, width, height, api == GE_GLFW_GLES);
}

__attribute__((constructor))
static void init(void) {
    if (ge_lib_available("libGL.so.1")) {
        gecnd_registry("set",  "backend:gl_glfw", NULL, NULL);
        gecnd_registry("hook", "backend:gl_glfw:pre_init", (void *)core_pre_init, (void *)(intptr_t)GE_GLFW_GL);
    }
    bool gles_libs = ge_lib_available("libEGL.so.1") &&
        (ge_lib_available("libGLESv2.so.2") || ge_lib_available("libGLESv2.so"));
    if (gles_libs) {
        gecnd_registry("set",  "backend:gles_glfw", NULL, NULL);
        gecnd_registry("hook", "backend:gles_glfw:pre_init", (void *)core_pre_init, (void *)(intptr_t)GE_GLFW_GLES);
    }
}
