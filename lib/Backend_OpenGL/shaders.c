// This file is designed to be included by a GL context provider (e.g., glfw.c, egl.c)
// It contains all shader compilation and initialization logic to avoid duplication.

// These headers are expected to be included by the parent file (glfw.c or egl.c)
// #include "gecnd.h"
// #include "gehook.h"
// #include "geopengl.h"


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
#include <gecnd/shadder_gl_video_vert.h>
#include <gecnd/shadder_gl_video_frag.h>
#include <gecnd/shadder_es_video_vert.h>
#include <gecnd/shadder_es_video_frag.h>

typedef struct {
    const char *name;
    const char *src;
    int len;
} shader_src_t;

#define SHADER(sym) { #sym, (const char*)(sym), (int)(sym##_len) }

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
        exit(1); // Or return 0 on EGL
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
        fprintf(stderr, "[PROGRAM LINK ERROR] VS: %s FS: %s %s", vs->name, fs->name, log);
        exit(1); // Or return 0 on EGL
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static inline const shader_src_t* pick_shader(bool is_gles, const shader_src_t *gl, const shader_src_t *es) {
    return is_gles ? es : gl;
}

static void init_all_shaders(bool is_gles) {
    GLBackendState *state = geogl_get_state();

    // Shape Program
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

    // Line Program
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

    // Texture Program
    shader_src_t tex_vs_gl = SHADER(shadder_gl_tex_vert);
    shader_src_t tex_fs_gl = SHADER(shadder_gl_tex_frag);
    shader_src_t tex_vs_es = SHADER(shadder_es_tex_vert);
    shader_src_t tex_fs_es = SHADER(shadder_es_tex_frag);
    state->texture_program = create_program(
        pick_shader(is_gles, &tex_vs_gl, &tex_vs_es),
        pick_shader(is_gles, &tex_fs_gl, &tex_fs_es)
    );
    state->texture_loc_pos      = glGetAttribLocation (state->texture_program, "a_pos");
    state->texture_loc_texCoord = glGetAttribLocation (state->texture_program, "a_texCoord");
    state->texture_loc_proj     = glGetUniformLocation(state->texture_program, "u_projection");
    state->texture_loc_sampler  = glGetUniformLocation(state->texture_program, "u_texture");

    // Unified Video/Texture Program
    shader_src_t video_vs_gl = SHADER(shadder_gl_video_vert);
    shader_src_t video_fs_gl = SHADER(shadder_gl_video_frag);
    shader_src_t video_vs_es = SHADER(shadder_es_video_vert);
    shader_src_t video_fs_es = SHADER(shadder_es_video_frag);
    state->video_program = create_program(
        pick_shader(is_gles, &video_vs_gl, &video_vs_es),
        pick_shader(is_gles, &video_fs_gl, &video_fs_es)
    );
    state->video_loc_pos      = glGetAttribLocation(state->video_program, "a_pos");
    state->video_loc_texCoord = glGetAttribLocation(state->video_program, "a_texCoord");
    state->video_loc_proj     = glGetUniformLocation(state->video_program, "u_projection");
    state->video_loc_tex_rgba = glGetUniformLocation(state->video_program, "tex_rgba");
    state->video_loc_tex_y    = glGetUniformLocation(state->video_program, "tex_y");
    state->video_loc_tex_u    = glGetUniformLocation(state->video_program, "tex_u");
    state->video_loc_tex_v    = glGetUniformLocation(state->video_program, "tex_v");
    state->video_loc_format   = glGetUniformLocation(state->video_program, "format");

    // Font Program
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
}

static void terminate_all_shaders(void) {
    GLBackendState *state = geogl_get_state();
    glDeleteProgram(state->shape_program);
    glDeleteProgram(state->line_program);
    glDeleteProgram(state->texture_program);
    glDeleteProgram(state->video_program);
    glDeleteProgram(state->font_program);
}
