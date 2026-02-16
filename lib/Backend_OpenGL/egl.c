#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <dlfcn.h>

#include <glad/egl.h>
#include <glad/gles2.h>

#include "gecnd.h"
#include "gehook.h"
#include "geopengl.h"

// From compiler.c - Shader sources
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
 * EGL Function Pointers & Loader
 * ========================================= */
static void* egl_lib = NULL;

// Declare all function pointers
static EGLint (*p_eglGetError)(void);
static EGLDisplay (*p_eglGetDisplay)(EGLNativeDisplayType);
static EGLBoolean (*p_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
static EGLBoolean (*p_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
static EGLSurface (*p_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint*);
static EGLContext (*p_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
static EGLBoolean (*p_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
static EGLBoolean (*p_eglDestroyContext)(EGLDisplay, EGLContext);
static EGLBoolean (*p_eglDestroySurface)(EGLDisplay, EGLSurface);
static EGLBoolean (*p_eglTerminate)(EGLDisplay);
static EGLBoolean (*p_eglSwapBuffers)(EGLDisplay, EGLSurface);
static void* (*p_eglGetProcAddress)(const char*);

static void load_egl_functions(void) {
    if (egl_lib) return;
    egl_lib = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!egl_lib) {
        egl_lib = dlopen("libEGL.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!egl_lib) {
        fprintf(stderr, "[FATAL] Could not dlopen libEGL.so.1 or libEGL.so\n");
        exit(1);
    }

    #define LOAD_EGL_FN(name) \
        p_##name = dlsym(egl_lib, #name); \
        if (!p_##name) { \
            fprintf(stderr, "[FATAL] Failed to load EGL function: %s (%s)\n", #name, dlerror()); \
            exit(1); \
        }

    LOAD_EGL_FN(eglGetError);
    LOAD_EGL_FN(eglGetDisplay);
    LOAD_EGL_FN(eglInitialize);
    LOAD_EGL_FN(eglChooseConfig);
    LOAD_EGL_FN(eglCreatePbufferSurface);
    LOAD_EGL_FN(eglCreateContext);
    LOAD_EGL_FN(eglMakeCurrent);
    LOAD_EGL_FN(eglDestroyContext);
    LOAD_EGL_FN(eglDestroySurface);
    LOAD_EGL_FN(eglTerminate);
    LOAD_EGL_FN(eglSwapBuffers);
    LOAD_EGL_FN(eglGetProcAddress);

    #undef LOAD_EGL_FN
}


/* =========================================
 * Global State
 * ========================================= */

static GLBackendState g_gl_state;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

GLBackendState* geogl_get_state(void) {
    return &g_gl_state;
}


/* =========================================
 * Platform Abstraction (EGL Implementation)
 * ========================================= */

static void die(const char *msg) {
    fprintf(stderr, "[FATAL] %s (EGL Error: %d)\n", msg, p_eglGetError());
    exit(1);
}

int platform_init(uint16_t width, uint16_t height) {
    load_egl_functions();

    GLBackendState *state = geogl_get_state();
    state->window_width = width;
    state->window_height = height;

    egl_display = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        die("EGL: p_eglGetDisplay failed");
    }

    if (!p_eglInitialize(egl_display, NULL, NULL)) {
        die("EGL: p_eglInitialize failed");
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_BLUE_SIZE,       8,
        EGL_GREEN_SIZE,      8,
        EGL_RED_SIZE,        8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      16,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_config;
    if (!p_eglChooseConfig(egl_display, attribs, &config, 1, &num_config)) {
        die("EGL: p_eglChooseConfig failed");
    }

    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE
    };
    egl_surface = p_eglCreatePbufferSurface(egl_display, config, pbuffer_attribs);
    if (egl_surface == EGL_NO_SURFACE) {
        die("EGL: p_eglCreatePbufferSurface failed");
    }

    const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = p_eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        die("EGL: p_eglCreateContext failed");
    }

    if (!p_eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        die("EGL: p_eglMakeCurrent failed");
    }

    return 0;
}

void platform_terminate(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        p_eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) p_eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) p_eglDestroySurface(egl_display, egl_surface);
        p_eglTerminate(egl_display);
    }
    egl_display = EGL_NO_DISPLAY;
    egl_context = EGL_NO_CONTEXT;
    egl_surface = EGL_NO_SURFACE;
}

void platform_swap_buffers(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        p_eglSwapBuffers(egl_display, egl_surface);
    }
}

void platform_poll_events(void) {
    // Input handling is platform-specific and not handled by EGL.
    // This would need to be implemented separately.
}

bool platform_should_close(void) {
    // Window lifecycle is platform-specific.
    return false; // Stub
}

void platform_set_swap_interval(int interval) {
    // eglSwapInterval is an extension, so we don't require it
}

double platform_get_time(void) {
    // A platform-specific timer is needed.
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void* platform_get_proc_address(const char *name) {
    return (void*)p_eglGetProcAddress(name);
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

static void opengl_init(void) {
    GLBackendState *state = geogl_get_state();

    if (!gladLoadGLES2((GLADloadfunc)platform_get_proc_address)) {
        fprintf(stderr, "[FATAL] GLAD GLES2 load failed");
        exit(1);
    }

    kv_init(state->textures);

    shader_src_t rect_vs = SHADER(shadder_es_rect_vert);
    shader_src_t rect_fs = SHADER(shadder_es_rect_frag);
    state->shape_program = create_program(&rect_vs, &rect_fs);
    state->shape_loc_pos    = glGetAttribLocation (state->shape_program, "a_pos");
    state->shape_loc_proj   = glGetUniformLocation(state->shape_program, "u_projection");
    state->shape_loc_color  = glGetUniformLocation(state->shape_program, "u_color");
    state->shape_loc_rect   = glGetUniformLocation(state->shape_program, "u_rect");
    state->shape_loc_radius = glGetUniformLocation(state->shape_program, "u_radius");
    state->shape_loc_mode   = glGetUniformLocation(state->shape_program, "u_mode");

    shader_src_t line_vs = SHADER(shadder_es_line_vert);
    shader_src_t line_fs = SHADER(shadder_es_line_frag);
    state->line_program = create_program(&line_vs, &line_fs);
    state->line_loc_pos   = glGetAttribLocation (state->line_program, "a_pos");
    state->line_loc_proj  = glGetUniformLocation(state->line_program, "u_projection");
    state->line_loc_color = glGetUniformLocation(state->line_program, "u_color");

    shader_src_t tex_vs = SHADER(shadder_es_tex_vert);
    shader_src_t tex_fs = SHADER(shadder_es_tex_frag);
    state->texture_program = create_program(&tex_vs, &tex_fs);
    state->texture_loc_pos     = glGetAttribLocation (state->texture_program, "a_pos");
    state->texture_loc_texCoord = glGetAttribLocation (state->texture_program, "a_texCoord");
    state->texture_loc_proj    = glGetUniformLocation(state->texture_program, "u_projection");
    state->texture_loc_sampler = glGetUniformLocation(state->texture_program, "u_texture");

    shader_src_t font_vs = SHADER(shadder_es_font_vert);
    shader_src_t font_fs = SHADER(shadder_es_font_frag);
    state->font_program = create_program(&font_vs, &font_fs);
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
    *key = NULL; // Stub: EGL does not handle input
}

/* =========================================
 * Render Implementation Files
 * ========================================= */

#include "render/draw.c"
#include "render/image.c"
#include "render/text.c"
