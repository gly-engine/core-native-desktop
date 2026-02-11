#include "backend_gl_internal.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include "gecnd.h"
#include "gehook.h"

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
#define KEYMAP_COUNT (sizeof(keymap) / sizeof(keymap[0]))

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    for (int i = 0; i < KEYMAP_COUNT; i++) {
        if (keymap[i].key == key) {
            gecnd_set_btn_state(gecnd_get_root(), keymap[i].name, action == GLFW_PRESS);
            return;
        }
    }
}

void gly_hook_input_keyboard(uint8_t index, char** key, bool* press) {
    glfwPollEvents();
    *key = NULL;
}

int platform_init(uint16_t width, uint16_t height) {
    GLBackendState *state = geogl_get_state();
    state->window_width = width;
    state->window_height = height;

    if (!glfwInit()) {
        die("GLFW init failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    state->window = glfwCreateWindow(
        state->window_width,
        state->window_height,
        "gecnd (OpenGL/GLFW)",
        NULL, NULL
    );

    if (!state->window) {
        die("window creation failed");
    }

    glfwMakeContextCurrent(state->window);
    glfwSetKeyCallback(state->window, key_callback);

    return 0;
}

void platform_terminate(void) {
    glfwTerminate();
}

void platform_swap_buffers(void) {
    glfwSwapBuffers(geogl_get_state()->window);
}

void platform_poll_events(void) {
}

bool platform_should_close(void) {
    return glfwWindowShouldClose(geogl_get_state()->window);
}

void platform_set_swap_interval(int interval) {
    glfwSwapInterval(interval);
}

double platform_get_time(void) {
    return glfwGetTime();
}

void* platform_get_proc_address(const char *name) {
    return (void*)glfwGetProcAddress(name);
}
