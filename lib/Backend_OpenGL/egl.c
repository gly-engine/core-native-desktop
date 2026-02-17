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
#include <gecnd/shadder_es_video_vert.h>
#include <gecnd/shadder_es_video_frag.h>

static GLBackendState g_gl_state;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

GLBackendState* geogl_get_state(void) {
    return &g_gl_state;
}

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
        fprintf(stderr, "[SHADER ERROR] %s (%s) %s\n", sh->name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint create_program(const shader_src_t *vs, const shader_src_t *fs) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    if (v == 0) return 0;

    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    if (f == 0) {
        glDeleteShader(v);
        return 0;
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[PROGRAM LINK ERROR] VS: %s\nFS: %s\n%s\n\n", vs->name, fs->name, log);
        glDeleteProgram(p);
        p = 0;
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static void opengl_terminate(void) {
    GLBackendState *state = geogl_get_state();
    glDeleteProgram(state->shape_program);
    glDeleteProgram(state->line_program);
    glDeleteProgram(state->texture_program);
    glDeleteProgram(state->video_program);
    glDeleteProgram(state->font_program);
    glDeleteBuffers(1, &state->vbo);
    kv_destroy(state->textures);
    native_text_terminate();
}

static void (*glad_gles2_loader(const char *name))(void) {
    void (*p)(void) = eglGetProcAddress(name);
    if (p) {
        return p;
    }

    static void* libgles = NULL;
    if (libgles == NULL) {
        libgles = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_GLOBAL);
        if (libgles == NULL) {
            libgles = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_GLOBAL);
        }
    }

    if (libgles != NULL) {
        return (void (*)(void)) dlsym(libgles, name);
    }

    return NULL;
}

static int opengl_init(void) {
    GLBackendState *state = geogl_get_state();

    if (!gladLoadGLES2((GLADloadfunc)glad_gles2_loader)) {
        fprintf(stderr, "[FATAL] GLAD GLES2 load failed\n");
        return -1;
    }

    kv_init(state->textures);

    shader_src_t rect_vs = SHADER(shadder_es_rect_vert);
    shader_src_t rect_fs = SHADER(shadder_es_rect_frag);
    state->shape_program = create_program(&rect_vs, &rect_fs);
    if (state->shape_program == 0) return -1;
    state->shape_loc_pos    = glGetAttribLocation (state->shape_program, "a_pos");
    state->shape_loc_proj   = glGetUniformLocation(state->shape_program, "u_projection");
    state->shape_loc_color  = glGetUniformLocation(state->shape_program, "u_color");
    state->shape_loc_rect   = glGetUniformLocation(state->shape_program, "u_rect");
    state->shape_loc_radius = glGetUniformLocation(state->shape_program, "u_radius");
    state->shape_loc_mode   = glGetUniformLocation(state->shape_program, "u_mode");

    shader_src_t line_vs = SHADER(shadder_es_line_vert);
    shader_src_t line_fs = SHADER(shadder_es_line_frag);
    state->line_program = create_program(&line_vs, &line_fs);
    if (state->line_program == 0) return -1;
    state->line_loc_pos   = glGetAttribLocation (state->line_program, "a_pos");
    state->line_loc_proj  = glGetUniformLocation(state->line_program, "u_projection");
    state->line_loc_color = glGetUniformLocation(state->line_program, "u_color");

    shader_src_t tex_vs = SHADER(shadder_es_tex_vert);
    shader_src_t tex_fs = SHADER(shadder_es_tex_frag);
    state->texture_program = create_program(&tex_vs, &tex_fs);
    if (state->texture_program == 0) return -1;
    state->texture_loc_pos     = glGetAttribLocation (state->texture_program, "a_pos");
    state->texture_loc_texCoord = glGetAttribLocation (state->texture_program, "a_texCoord");
    state->texture_loc_proj    = glGetUniformLocation(state->texture_program, "u_projection");
    state->texture_loc_sampler = glGetUniformLocation(state->texture_program, "u_texture");

    init_video_program(true); // is_gles = true

    shader_src_t font_vs = SHADER(shadder_es_font_vert);
    shader_src_t font_fs = SHADER(shadder_es_font_frag);
    state->font_program = create_program(&font_vs, &font_fs);
    if (state->font_program == 0) return -1;
    state->font_loc_pos      = glGetAttribLocation (state->font_program, "a_pos");
    state->font_loc_texCoord = glGetAttribLocation (state->font_program, "a_texCoord");
    state->font_loc_proj     = glGetUniformLocation(state->font_program, "u_projection");
    state->font_loc_sampler  = glGetUniformLocation(state->font_program, "u_texture");
    state->font_loc_color    = glGetUniformLocation(state->font_program, "u_color");

    glGenBuffers(1, &state->vbo);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return 0;
}

void platform_terminate(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
    }
    egl_display = EGL_NO_DISPLAY;
    egl_context = EGL_NO_CONTEXT;
    egl_surface = EGL_NO_SURFACE;
}

int platform_init(uint16_t width, uint16_t height) {
    if (!gladLoaderLoadEGL(EGL_DEFAULT_DISPLAY)) {
        fprintf(stderr, "[FATAL] Failed to load EGL functions\n");
        return -1;
    }

    GLBackendState *state = geogl_get_state();
    state->window_width = width;
    state->window_height = height;

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "[FATAL] eglGetDisplay failed (EGL Error: %d)\n", eglGetError());
        return -1;
    }

    if (!eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "[FATAL] eglInitialize failed (EGL Error: %d)\n", eglGetError());
        return -1;
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_BLUE_SIZE,       8,
        EGL_GREEN_SIZE,      8,
        EGL_RED_SIZE,        8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      16,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_config;
    if (!eglChooseConfig(egl_display, attribs, &config, 1, &num_config)) {
        fprintf(stderr, "[FATAL] eglChooseConfig failed (EGL Error: %d)\n", eglGetError());
        return -1;
    }

    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)0, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[FATAL] eglCreateWindowSurface failed (EGL Error: %d)\n", eglGetError());
        return -1;
    }

    const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "[FATAL] eglCreateContext failed (EGL Error: %d)\n", eglGetError());
        return -1;
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "[FATAL] eglMakeCurrent failed (EGL Error: %d)\n", eglGetError());
        return -1;
    }

    if (opengl_init() != 0) {
        platform_terminate();
        return -1;
    }

    return 0;
}

void platform_swap_buffers(void) {
    if (egl_display != EGL_NO_DISPLAY) {
        eglSwapBuffers(egl_display, egl_surface);
    }
}

void platform_poll_events(void) {}

bool platform_should_close(void) {
    return false;
}

void platform_set_swap_interval(int interval) {}

double platform_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void gly_hook_display_init(uint16_t width, uint16_t height) {
    GLBackendState *state = geogl_get_state();
    if (platform_init(width, height) != 0) {
        fprintf(stderr, "[FATAL] Platform initialization failed.\n");
        exit(1);
    }
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

#include "render/media.c"
#include "render/draw.c"
#include "render/image.c"
#include "render/text.c"
