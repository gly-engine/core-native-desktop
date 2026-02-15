#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        fprintf(stderr, "[SHADER ERROR] %s (%s)\n%s", sh->name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
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
        fprintf(stderr, "[PROGRAM LINK ERROR]\nVS: %s\nFS: %s\n%s\n\n", vs->name, fs->name, log);
        exit(1);
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static inline const shader_src_t* pick_shader(bool is_gles, const shader_src_t *gl, const shader_src_t *es) {
    return is_gles ? es : gl;
}

void opengl_init(void) {
    GLBackendState *state = geogl_get_state();

    if (!gladLoadGL((GLADloadfunc)platform_get_proc_address)) {
        fprintf(stderr, "[FATAL] GLAD load failed");
        exit(1);
    }

    const char *ver = (const char*)glGetString(GL_VERSION);
    bool is_gles = ver && strstr(ver, "OpenGL ES");

    // Initialize kvec for textures
    kv_init(state->textures);

    /* ===== Rect program ===== */
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

    /* ===== Line program ===== */
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

    /* ===== Texture program ===== */
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

    /* ===== Font program ===== */
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

    /* ===== VBO ===== */
    glGenBuffers(1, &state->vbo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void opengl_terminate(void) {
    GLBackendState *state = geogl_get_state();
    glDeleteProgram(state->shape_program);
    glDeleteProgram(state->line_program);
    glDeleteProgram(state->texture_program);
    glDeleteProgram(state->font_program);
    glDeleteBuffers(1, &state->vbo);
    kv_destroy(state->textures); // Destroy kvec for textures

    native_text_terminate();
}
