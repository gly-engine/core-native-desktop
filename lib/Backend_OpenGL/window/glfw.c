#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"
#include "gebuffer.h"
#include "gamely_media.h"
/** @todo move — forward decl para evitar include cruzado Backend↔Frontend */
void libretro_hw_gl_ready(void);

static GLBackendState g_gl_state;

GLBackendState* geogl_get_state(void) {
    return &g_gl_state;
}

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
    (void)window; /** @todo nao sei o porque disso.
    if (!focused) {
        for (size_t i = 0; i < sizeof(keymap)/sizeof(keymap[0]); i++) {
            gecnd_set_btn_state(gecnd_get_root(), keymap[i].name, false);
        }
    }*/
}

void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int platform_init(uint16_t width, uint16_t height) {
    GLBackendState *state = geogl_get_state();
    state->window_width = width;
    state->window_height = height;

    glfwSetErrorCallback(glfw_error_callback);

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
    glfwSetWindowFocusCallback(state->window, focus_callback);

    return 0;
}

void platform_terminate(void) {
    glfwTerminate();
}

void platform_swap_buffers(void) {
    glfwSwapBuffers(geogl_get_state()->window);
}

double platform_get_time(void) {
    return glfwGetTime();
}

void* platform_get_proc_address(const char *name) {
    return (void*)glfwGetProcAddress(name);
}

void gly_hook_display_init(uint16_t width, uint16_t height) {
    GLBackendState *s = geogl_get_state();
    if (platform_init(width, height) != 0) exit(1);
    if (!gladLoadGL((GLADloadfunc)platform_get_proc_address)) die("GLAD load failed");
    const char *ver = (const char*)glGetString(GL_VERSION);
    bool gles = ver && strstr(ver, "OpenGL ES");
    kv_init(s->textures);
    init_all_shaders(gles);
    ge_pipeline_init(width, height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mat4_ortho(s->projection, 0, width, height, 0, -(float)GE_MAX_LAYERS, (float)GE_MAX_LAYERS);
    s->last_frame_time = platform_get_time();
    libretro_hw_gl_ready();

    gamely_daemon_input_add_class("glfw");
    for (uint8_t i = 0; i < sizeof(keymap)/sizeof(*keymap); i++) {
        gamely_daemon_input_add_keycode(keymap[i].name, keymap[i].key);
    }
}

void gly_hook_display_dt(int16_t *delta_time) {
    GLBackendState *state = geogl_get_state();
    double t = platform_get_time();
    *delta_time = (int16_t)((t - state->last_frame_time) * 1000.0);
    state->last_frame_time = t;
}

void gly_hook_should_close(bool *should_close) {
    if (geogl_get_state()->window) {
        *should_close = glfwWindowShouldClose(geogl_get_state()->window);
    }
}

void gly_hook_display_close(void) {
    terminate_all_shaders();
    ge_pipeline_terminate();
    gamely_daemon_media_shutdown();
    native_text_terminate();
    platform_terminate();
}

void gly_hook_input_keyboard(uint8_t index, char** key, bool* press) {
    (void)index; (void)key; (void)press;
    glfwPollEvents();
}
