#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"

/* =========================================
 * Global State
 * ========================================= */

static GLBackendState g_gl_state;

GLBackendState* geogl_get_state(void) {
    return &g_gl_state;
}


/* =========================================
 * Platform Abstraction (GLFW Implementation)
 * ========================================= */

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
    glfwPollEvents();
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


/* =========================================
 * OpenGL Specifics (Shader Compilation, etc.)
 * ========================================= */

void native_text_terminate();

#include "shaders.c"

static void opengl_init(void) {
    GLBackendState *state = geogl_get_state();

    if (!gladLoadGL((GLADloadfunc)platform_get_proc_address)) {
        fprintf(stderr, "[FATAL] GLAD load failed");
        exit(1);
    }

    const char *ver = (const char*)glGetString(GL_VERSION);
    bool is_gles = ver && strstr(ver, "OpenGL ES");

    kv_init(state->textures);
    init_all_shaders(is_gles);

    glGenBuffers(1, &state->vbo);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void opengl_terminate(void) {
    GLBackendState *state = geogl_get_state();
    terminate_all_shaders();
    glDeleteBuffers(1, &state->vbo);
    if (state->aa_fbo) glDeleteFramebuffers(1, &state->aa_fbo);
    if (state->aa_fbo_texture) glDeleteTextures(1, &state->aa_fbo_texture);
    if (state->video_textures[0]) glDeleteTextures(3, state->video_textures);
    kv_destroy(state->textures);
    native_text_terminate();
}


/* =========================================
 * Core Hooks
 * ========================================= */
 
void gly_hook_display_init(uint16_t width, uint16_t height) {
    GLBackendState *state = geogl_get_state();

    if (platform_init(width, height) != 0) {
        fprintf(stderr, "[FATAL] Platform initialization failed.\n");
        exit(1);
    }

    opengl_init();

    native_draw_color(0xFFFFFFFF);
    native_draw_clear(0x1A2B3CFF);

    mat4_ortho(state->projection, 0, width, height, 0, -1, 1);
    
    state->last_frame_time = platform_get_time();
}

void gly_hook_display_fps(uint8_t fps) {
    platform_set_swap_interval(fps == 0 ? 0 : 1);
}

void gly_hook_display_dt(int16_t *delta_time) {
    GLBackendState *state = geogl_get_state();
    double t = platform_get_time();
    *delta_time = (int16_t)((t - state->last_frame_time) * 1000.0);
    state->last_frame_time = t;
}

void gly_hook_should_close(bool *should_close) {
    *should_close = platform_should_close();
}

void gly_hook_display_close(void) {
    opengl_terminate();
    platform_terminate();
}

void gly_hook_input_keyboard(uint8_t index, char** key, bool* press) {
    platform_poll_events();
    *key = NULL;
}

/* =========================================
 * Render Implementation Files
 * ========================================= */

#include "render/media.c"
#include "render/draw.c"
#include "render/image.c"
#include "render/text.c"
