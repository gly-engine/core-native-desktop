#include "backend_gl_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================
   Generated shaders
   ===================== */
#include <gecnd/shadder_gl_rect_vert.h>
#include <gecnd/shadder_gl_rect_frag.h>
#include <gecnd/shadder_gl_line_vert.h>
#include <gecnd/shadder_gl_line_frag.h>
#include <gecnd/shadder_es_rect_vert.h>
#include <gecnd/shadder_es_rect_frag.h>
#include <gecnd/shadder_es_line_vert.h>
#include <gecnd/shadder_es_line_frag.h>

/* =====================
   Global state
   ===================== */
GLFWwindow *g_window = NULL;
uint16_t g_window_width  = 1280;
uint16_t g_window_height = 720;

/* Rect drawing */
GLuint g_shape_program;
GLint  g_shape_loc_pos;
GLint  g_shape_loc_proj;
GLint  g_shape_loc_color;
GLint  g_shape_loc_rect;
GLint  g_shape_loc_radius;
GLint  g_shape_loc_mode;

/* Line drawing */
GLuint g_line_program;
GLint  g_line_loc_pos;
GLint  g_line_loc_proj;
GLint  g_line_loc_color;

GLuint g_vbo;

/* Drawing state */
uint32_t g_current_color = 0xFFFFFFFF;
uint32_t g_clear_color   = 0x1A2B3CFF;

/* Timing */
double g_last_frame_time = 0.0;

/* =====================
   Helpers
   ===================== */
static void die(const char *msg)
{
    fprintf(stderr, "[FATAL] %s\n", msg);
    exit(1);
}

typedef struct {
    const char *name;
    const char *src;
    int len;
} shader_src_t;

#define SHADER(sym) \
    { #sym, (const char*)(sym), (int)(sym##_len) }

static GLuint compile_shader(GLenum type, const shader_src_t *sh)
{
    GLuint s = glCreateShader(type);

    glShaderSource(s, 1, &sh->src, &sh->len);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr,
            "[SHADER ERROR] %s (%s)\n%s\n",
            sh->name,
            type == GL_VERTEX_SHADER ? "vertex" : "fragment",
            log
        );
        exit(1);
    }
    return s;
}

static GLuint create_program(const shader_src_t *vs,
                             const shader_src_t *fs)
{
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
        fprintf(stderr,
            "[PROGRAM LINK ERROR]\nVS: %s\nFS: %s\n%s\n",
            vs->name, fs->name, log
        );
        exit(1);
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static inline const shader_src_t *
pick_shader(bool is_gles,
            const shader_src_t *gl,
            const shader_src_t *es)
{
    return is_gles ? es : gl;
}

/* =====================
   Hooks
   ===================== */
void gly_hook_display_init(uint16_t width, uint16_t height)
{
    g_window_width  = width;
    g_window_height = height;

    if (!glfwInit())
        die("GLFW init failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    g_window = glfwCreateWindow(
        g_window_width,
        g_window_height,
        "gecnd (OpenGL)",
        NULL, NULL
    );

    if (!g_window)
        die("window creation failed");

    glfwMakeContextCurrent(g_window);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress))
        die("GLAD load failed");

    const char *ver = (const char*)glGetString(GL_VERSION);
    bool is_gles = ver && strstr(ver, "OpenGL ES");

    /* ===== Rect program ===== */
    shader_src_t rect_vs_gl = SHADER(shadder_gl_rect_vert);
    shader_src_t rect_fs_gl = SHADER(shadder_gl_rect_frag);
    shader_src_t rect_vs_es = SHADER(shadder_es_rect_vert);
    shader_src_t rect_fs_es = SHADER(shadder_es_rect_frag);

    g_shape_program = create_program(
        pick_shader(is_gles, &rect_vs_gl, &rect_vs_es),
        pick_shader(is_gles, &rect_fs_gl, &rect_fs_es)
    );

    g_shape_loc_pos    = glGetAttribLocation (g_shape_program, "a_pos");
    g_shape_loc_proj   = glGetUniformLocation(g_shape_program, "u_projection");
    g_shape_loc_color  = glGetUniformLocation(g_shape_program, "u_color");
    g_shape_loc_rect   = glGetUniformLocation(g_shape_program, "u_rect");
    g_shape_loc_radius = glGetUniformLocation(g_shape_program, "u_radius");
    g_shape_loc_mode   = glGetUniformLocation(g_shape_program, "u_mode");

    /* ===== Line program ===== */
    shader_src_t line_vs_gl = SHADER(shadder_gl_line_vert);
    shader_src_t line_fs_gl = SHADER(shadder_gl_line_frag);
    shader_src_t line_vs_es = SHADER(shadder_es_line_vert);
    shader_src_t line_fs_es = SHADER(shadder_es_line_frag);

    g_line_program = create_program(
        pick_shader(is_gles, &line_vs_gl, &line_vs_es),
        pick_shader(is_gles, &line_fs_gl, &line_fs_es)
    );

    g_line_loc_pos   = glGetAttribLocation (g_line_program, "a_pos");
    g_line_loc_proj  = glGetUniformLocation(g_line_program, "u_projection");
    g_line_loc_color = glGetUniformLocation(g_line_program, "u_color");

    /* ===== VBO ===== */
    glGenBuffers(1, &g_vbo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void gly_hook_display_fps(uint8_t fps)
{
    glfwSwapInterval(fps == 0 ? 0 : 1);
}

void gly_hook_display_dt(int16_t *delta_time)
{
    double t = glfwGetTime();
    *delta_time = (int16_t)((t - g_last_frame_time) * 1000.0);
    g_last_frame_time = t;
}

void gly_hook_should_close(bool *should_close)
{
    *should_close = glfwWindowShouldClose(g_window);
}

void gly_hook_display_close(void)
{
    glDeleteProgram(g_shape_program);
    glDeleteProgram(g_line_program);
    glDeleteBuffers(1, &g_vbo);
    glfwTerminate();
}

/* =====================
   Native drawing
   ===================== */
void native_draw_start(void)
{
    glfwPollEvents();

    float r = ((g_clear_color >> 24) & 0xFF) / 255.0f;
    float g = ((g_clear_color >> 16) & 0xFF) / 255.0f;
    float b = ((g_clear_color >>  8) & 0xFF) / 255.0f;
    float a = ( g_clear_color        & 0xFF) / 255.0f;

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void native_draw_flush(void)
{
    glfwSwapBuffers(g_window);
}

void native_draw_color(uint32_t color)
{
    g_current_color = color;
}

void native_draw_clear(uint32_t color)
{
    g_clear_color = color;
}
