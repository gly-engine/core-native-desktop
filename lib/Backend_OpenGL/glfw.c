#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"

// From compiler.c
#include <gecnd/shadder_gl_rect_vert.h>
#include <gecnd/shadder_gl_rect_frag.h>
#include <gecnd/shadder_gl_line_vert.h>
#include <gecnd/shadder_gl_line_frag.h>
#include <gecnd/shadder_gl_tex_vert.h>
#include <gecnd/shadder_gl_tex_frag.h>
#include <gecnd/shadder_es_rect_vert.h>
#include <gecnd/shadder_es_rect_frag.h>
#include <gecnd/shadder_es_line_vert.h>
#include <gecnd/shadder_es_line_frag.h>
#include <gecnd/shadder_es_tex_vert.h>
#include <gecnd/shadder_es_tex_frag.h>
#include <gecnd/shadder_gl_font_vert.h>
#include <gecnd/shadder_gl_font_frag.h>
#include <gecnd/shadder_es_font_vert.h>
#include <gecnd/shadder_es_font_frag.h>


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

typedef struct {
    const char *name;
    const char *src;
    int len;
} shader_src_t;

#define SHADER(sym) { #sym, (const char*)(sym), (int)(sym##_len) }

void native_text_terminate();

static GLuint compile_shader(GLenum type, const shader_src_t *sh) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &sh->src, &sh->len);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[SHADER ERROR] %s (%s) %s", sh->name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        exit(1);
    }
    return s;
}

static GLuint create_program(const shader_src_t *vs, const shader_src_t *fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);

    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[PROGRAM LINK ERROR] VS: %s\nFS: %s\n%s\n\n", vs->name, fs->name, log);
        exit(1);
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static inline const shader_src_t* pick_shader(bool is_gles, const shader_src_t *gl, const shader_src_t *es) {
    return is_gles ? es : gl;
}

static void opengl_init(void) {
    GLBackendState *state = geogl_get_state();

    if (!gladLoadGL((GLADloadfunc)platform_get_proc_address)) {
        fprintf(stderr, "[FATAL] GLAD load failed");
        exit(1);
    }

    const char *ver = (const char*)glGetString(GL_VERSION);
    bool is_gles = ver && strstr(ver, "OpenGL ES");

    kv_init(state->textures);

    shader_src_t rect_vs_gl = SHADER(shadder_gl_rect_vert);
    shader_src_t rect_fs_gl = SHADER(shadder_gl_rect_frag);
    shader_src_t rect_vs_es = SHADER(shadder_es_rect_vert);
    shader_src_t rect_fs_es = SHADER(shadder_es_rect_frag);

    state->shape_program = create_program(
        pick_shader(is_gles, &rect_vs_gl, &rect_vs_es),
        pick_shader(is_gles, &rect_fs_gl, &rect_fs_es)
    );

    state->shape_loc_pos    = glGetAttribLocation (state->shape_program, "a_pos");
    state->shape_loc_proj   = glGetUniformLocation(state->shape_program, "u_projection");
    state->shape_loc_color  = glGetUniformLocation(state->shape_program, "u_color");
    state->shape_loc_rect   = glGetUniformLocation(state->shape_program, "u_rect");
    state->shape_loc_radius = glGetUniformLocation(state->shape_program, "u_radius");
    state->shape_loc_mode   = glGetUniformLocation(state->shape_program, "u_mode");

    shader_src_t line_vs_gl = SHADER(shadder_gl_line_vert);
    shader_src_t line_fs_gl = SHADER(shadder_gl_line_frag);
    shader_src_t line_vs_es = SHADER(shadder_es_line_vert);
    shader_src_t line_fs_es = SHADER(shadder_es_line_frag);

    state->line_program = create_program(
        pick_shader(is_gles, &line_vs_gl, &line_vs_es),
        pick_shader(is_gles, &line_fs_gl, &line_fs_es)
    );

    state->line_loc_pos   = glGetAttribLocation (state->line_program, "a_pos");
    state->line_loc_proj  = glGetUniformLocation(state->line_program, "u_projection");
    state->line_loc_color = glGetUniformLocation(state->line_program, "u_color");

    shader_src_t tex_vs_gl = SHADER(shadder_gl_tex_vert);
    shader_src_t tex_fs_gl = SHADER(shadder_gl_tex_frag);
    shader_src_t tex_vs_es = SHADER(shadder_es_tex_vert);
    shader_src_t tex_fs_es = SHADER(shadder_es_tex_frag);

    state->texture_program = create_program(
        pick_shader(is_gles, &tex_vs_gl, &tex_vs_es),
        pick_shader(is_gles, &tex_fs_gl, &tex_fs_es)
    );

    state->texture_loc_pos     = glGetAttribLocation (state->texture_program, "a_pos");
    state->texture_loc_texCoord = glGetAttribLocation (state->texture_program, "a_texCoord");
    state->texture_loc_proj    = glGetUniformLocation(state->texture_program, "u_projection");
    state->texture_loc_sampler = glGetUniformLocation(state->texture_program, "u_texture");

    shader_src_t font_vs_gl = SHADER(shadder_gl_font_vert);
    shader_src_t font_fs_gl = SHADER(shadder_gl_font_frag);
    shader_src_t font_vs_es = SHADER(shadder_es_font_vert);
    shader_src_t font_fs_es = SHADER(shadder_es_font_frag);

    state->font_program = create_program(
        pick_shader(is_gles, &font_vs_gl, &font_vs_es),
        pick_shader(is_gles, &font_fs_gl, &font_fs_es)
    );

    state->font_loc_pos      = glGetAttribLocation (state->font_program, "a_pos");
    state->font_loc_texCoord = glGetAttribLocation (state->font_program, "a_texCoord");
    state->font_loc_proj     = glGetUniformLocation(state->font_program, "u_projection");
    state->font_loc_sampler  = glGetUniformLocation(state->font_program, "u_texture");
    state->font_loc_color    = glGetUniformLocation(state->font_program, "u_color");

    glGenBuffers(1, &state->vbo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void opengl_terminate(void) {
    GLBackendState *state = geogl_get_state();
    glDeleteProgram(state->shape_program);
    glDeleteProgram(state->line_program);
    glDeleteProgram(state->texture_program);
    glDeleteProgram(state->font_program);
    glDeleteBuffers(1, &state->vbo);
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

#include "render/draw.c"
#include "render/image.c"
#include "render/text.c"
