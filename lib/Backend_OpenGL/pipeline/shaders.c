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
#include <gecnd/shadder_gl_video_vert.h>
#include <gecnd/shadder_gl_video_frag.h>
#include <gecnd/shadder_es_video_vert.h>
#include <gecnd/shadder_es_video_frag.h>
#include <gecnd/shadder_es_aa_vert.h>
#include <gecnd/shadder_es_aa_frag.h>
#include <gecnd/shadder_gl_aa_vert.h>
#include <gecnd/shadder_gl_aa_frag.h>

typedef struct {
    const char *name;
    const char *src;
    int len;
} shader_src_t;

#define SHADER(sym) { #sym, (const char*)(sym), (int)(sym##_len) }

static GLuint compile(GLenum type, const char* src, int len, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, len > 0 ? &len : NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, NULL, log);
        fprintf(stderr, "[SHADER %s] %s\n", name, log);
        exit(1);
    }
    return s;
}

static GLuint create_prog(const shader_src_t* vs, const shader_src_t* fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs->src, vs->len, vs->name);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs->src, fs->len, fs->name);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, 1024, NULL, log);
        fprintf(stderr, "[LINK %s/%s] %s\n", vs->name, fs->name, log);
        exit(1);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static inline const shader_src_t* pick(bool gles, const shader_src_t *gl, const shader_src_t *es) {
    return gles ? es : gl;
}

void init_all_shaders(bool gles) {
    GLBackendState *s = geogl_get_state();

    shader_src_t rs_vs_gl = SHADER(shadder_gl_rect_vert);
    shader_src_t rs_fs_gl = SHADER(shadder_gl_rect_frag);
    shader_src_t rs_vs_es = SHADER(shadder_es_rect_vert);
    shader_src_t rs_fs_es = SHADER(shadder_es_rect_frag);
    s->shape_program = create_prog(pick(gles, &rs_vs_gl, &rs_vs_es), pick(gles, &rs_fs_gl, &rs_fs_es));
    s->shape_loc_pos = glGetAttribLocation(s->shape_program, "a_pos");
    s->shape_loc_proj = glGetUniformLocation(s->shape_program, "u_projection");
    s->shape_loc_color = glGetUniformLocation(s->shape_program, "u_color");
    s->shape_loc_rect = glGetUniformLocation(s->shape_program, "u_rect");
    s->shape_loc_radius = glGetUniformLocation(s->shape_program, "u_radius");
    s->shape_loc_mode = glGetUniformLocation(s->shape_program, "u_mode");
    s->shape_loc_thickness = glGetUniformLocation(s->shape_program, "u_thickness");

    shader_src_t ls_vs_gl = SHADER(shadder_gl_line_vert);
    shader_src_t ls_fs_gl = SHADER(shadder_gl_line_frag);
    shader_src_t ls_vs_es = SHADER(shadder_es_line_vert);
    shader_src_t ls_fs_es = SHADER(shadder_es_line_frag);
    s->line_program = create_prog(pick(gles, &ls_vs_gl, &ls_vs_es), pick(gles, &ls_fs_gl, &ls_fs_es));
    s->line_loc_pos = glGetAttribLocation(s->line_program, "a_pos");
    s->line_loc_proj = glGetUniformLocation(s->line_program, "u_projection");
    s->line_loc_color = glGetUniformLocation(s->line_program, "u_color");

    shader_src_t ts_vs_gl = SHADER(shadder_gl_tex_vert);
    shader_src_t ts_fs_gl = SHADER(shadder_gl_tex_frag);
    shader_src_t ts_vs_es = SHADER(shadder_es_tex_vert);
    shader_src_t ts_fs_es = SHADER(shadder_es_tex_frag);
    s->texture_program = create_prog(pick(gles, &ts_vs_gl, &ts_vs_es), pick(gles, &ts_fs_gl, &ts_fs_es));
    s->texture_loc_pos = glGetAttribLocation(s->texture_program, "a_pos");
    s->texture_loc_texCoord = glGetAttribLocation(s->texture_program, "a_texCoord");
    s->texture_loc_proj = glGetUniformLocation(s->texture_program, "u_projection");
    s->texture_loc_sampler = glGetUniformLocation(s->texture_program, "u_texture");

    shader_src_t vs_vs_gl = SHADER(shadder_gl_video_vert);
    shader_src_t vs_fs_gl = SHADER(shadder_gl_video_frag);
    shader_src_t vs_vs_es = SHADER(shadder_es_video_vert);
    shader_src_t vs_fs_es = SHADER(shadder_es_video_frag);
    s->video_program = create_prog(pick(gles, &vs_vs_gl, &vs_vs_es), pick(gles, &vs_fs_gl, &vs_fs_es));
    s->video_loc_pos = glGetAttribLocation(s->video_program, "a_pos");
    s->video_loc_texCoord = glGetAttribLocation(s->video_program, "a_texCoord");
    s->video_loc_proj = glGetUniformLocation(s->video_program, "u_projection");
    s->video_loc_tex_rgba = glGetUniformLocation(s->video_program, "tex_rgba");
    s->video_loc_tex_y = glGetUniformLocation(s->video_program, "tex_y");
    s->video_loc_tex_u = glGetUniformLocation(s->video_program, "tex_u");
    s->video_loc_tex_v = glGetUniformLocation(s->video_program, "tex_v");
    s->video_loc_format = glGetUniformLocation(s->video_program, "format");

    shader_src_t fs_vs_gl = SHADER(shadder_gl_font_vert);
    shader_src_t fs_fs_gl = SHADER(shadder_gl_font_frag);
    shader_src_t fs_vs_es = SHADER(shadder_es_font_vert);
    shader_src_t fs_fs_es = SHADER(shadder_es_font_frag);
    s->font_program = create_prog(pick(gles, &fs_vs_gl, &fs_vs_es), pick(gles, &fs_fs_gl, &fs_fs_es));
    s->font_loc_pos = glGetAttribLocation(s->font_program, "a_pos");
    s->font_loc_texCoord = glGetAttribLocation(s->font_program, "a_texCoord");
    s->font_loc_proj = glGetUniformLocation(s->font_program, "u_projection");
    s->font_loc_sampler = glGetUniformLocation(s->font_program, "u_texture");
    s->font_loc_color = glGetUniformLocation(s->font_program, "u_color");

    shader_src_t as_vs_gl = SHADER(shadder_gl_aa_vert);
    shader_src_t as_fs_gl = SHADER(shadder_gl_aa_frag);
    shader_src_t as_vs_es = SHADER(shadder_es_aa_vert);
    shader_src_t as_fs_es = SHADER(shadder_es_aa_frag);
    s->aa_program = create_prog(pick(gles, &as_vs_gl, &as_vs_es), pick(gles, &as_fs_gl, &as_fs_es));
    s->aa_loc_pos = glGetAttribLocation(s->aa_program, "a_pos");
    s->aa_loc_texCoord = glGetAttribLocation(s->aa_program, "a_texCoord");
    s->aa_loc_proj = glGetUniformLocation(s->aa_program, "u_projection");
    s->aa_loc_sampler = glGetUniformLocation(s->aa_program, "u_texture");
    s->aa_loc_tsize = glGetUniformLocation(s->aa_program, "u_texelSize");
    s->aa_loc_blur = glGetUniformLocation(s->aa_program, "u_aa_blur");
    s->aa_loc_wC = glGetUniformLocation(s->aa_program, "u_aa_wC");
    s->aa_loc_wN = glGetUniformLocation(s->aa_program, "u_aa_wN");
    s->aa_loc_bright = glGetUniformLocation(s->aa_program, "u_brightness");
    s->aa_loc_contrast = glGetUniformLocation(s->aa_program, "u_contrast");
    s->aa_loc_sat = glGetUniformLocation(s->aa_program, "u_saturation");
    s->aa_loc_grain = glGetUniformLocation(s->aa_program, "u_film_grain");
    s->aa_loc_sharpen = glGetUniformLocation(s->aa_program, "u_sharpen");
}

void terminate_all_shaders(void) {
    GLBackendState *s = geogl_get_state();
    glDeleteProgram(s->shape_program);
    glDeleteProgram(s->line_program);
    glDeleteProgram(s->texture_program);
    glDeleteProgram(s->video_program);
    glDeleteProgram(s->font_program);
    glDeleteProgram(s->aa_program);
}
