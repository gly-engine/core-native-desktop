#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "geopengl.h"

#include <gecnd/shadder_gl_draw_vert.h>
#include <gecnd/shadder_gl_draw_frag.h>
#include <gecnd/shadder_es_draw_vert.h>
#include <gecnd/shadder_es_draw_frag.h>
#include <gecnd/shadder_gl_video_vert.h>
#include <gecnd/shadder_gl_video_frag.h>
#include <gecnd/shadder_es_video_vert.h>
#include <gecnd/shadder_es_video_frag.h>
#include <gecnd/shadder_es_post_vert.h>
#include <gecnd/shadder_es_post_frag.h>
#include <gecnd/shadder_gl_post_vert.h>
#include <gecnd/shadder_gl_post_frag.h>

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

static GLuint create_prog(const shader_src_t* vs, const shader_src_t* fs, bool is_draw) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs->src, vs->len, vs->name);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs->src, fs->len, fs->name);
    glAttachShader(p, v);
    glAttachShader(p, f);
    
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glBindAttribLocation(p, 2, "a_color");
    if (is_draw) {
        glBindAttribLocation(p, 3, "a_local");
        glBindAttribLocation(p, 4, "a_size");
        glBindAttribLocation(p, 5, "a_sdf");
    }

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

    shader_src_t ds_vs_gl = SHADER(shadder_gl_draw_vert);
    shader_src_t ds_fs_gl = SHADER(shadder_gl_draw_frag);
    shader_src_t ds_vs_es = SHADER(shadder_es_draw_vert);
    shader_src_t ds_fs_es = SHADER(shadder_es_draw_frag);
    s->draw_program = create_prog(pick(gles, &ds_vs_gl, &ds_vs_es), pick(gles, &ds_fs_gl, &ds_fs_es), true);
    s->draw_loc_proj = glGetUniformLocation(s->draw_program, "u_mvp");
    s->draw_loc_atlas = glGetUniformLocation(s->draw_program, "u_atlas");

    shader_src_t vs_vs_gl = SHADER(shadder_gl_video_vert);
    shader_src_t vs_fs_gl = SHADER(shadder_gl_video_frag);
    shader_src_t vs_vs_es = SHADER(shadder_es_video_vert);
    shader_src_t vs_fs_es = SHADER(shadder_es_video_frag);
    s->video_program = create_prog(pick(gles, &vs_vs_gl, &vs_vs_es), pick(gles, &vs_fs_gl, &vs_fs_es), false);
    s->video_loc_pos = 0;
    s->video_loc_texCoord = 1;
    s->video_loc_proj = glGetUniformLocation(s->video_program, "u_projection");
    s->video_loc_tex_rgba = glGetUniformLocation(s->video_program, "tex_rgba");
    s->video_loc_tex_y = glGetUniformLocation(s->video_program, "tex_y");
    s->video_loc_tex_u = glGetUniformLocation(s->video_program, "tex_u");
    s->video_loc_tex_v = glGetUniformLocation(s->video_program, "tex_v");
    s->video_loc_format = glGetUniformLocation(s->video_program, "format");
    s->video_loc_bright = glGetUniformLocation(s->video_program, "u_brightness");
    s->video_loc_contrast = glGetUniformLocation(s->video_program, "u_contrast");
    s->video_loc_sat = glGetUniformLocation(s->video_program, "u_saturation");
    s->video_loc_grain = glGetUniformLocation(s->video_program, "u_film_grain");
    s->video_loc_sharpen = glGetUniformLocation(s->video_program, "u_sharpen");
    s->video_loc_tsize = glGetUniformLocation(s->video_program, "u_texelSize");
    s->video_loc_time = glGetUniformLocation(s->video_program, "u_time");
    s->video_loc_scratch = glGetUniformLocation(s->video_program, "u_scratch");
    s->video_loc_jitter = glGetUniformLocation(s->video_program, "u_jitter");

    shader_src_t as_vs_gl = SHADER(shadder_gl_post_vert);
    shader_src_t as_fs_gl = SHADER(shadder_gl_post_frag);
    shader_src_t as_vs_es = SHADER(shadder_es_post_vert);
    shader_src_t as_fs_es = SHADER(shadder_es_post_frag);
    s->post_program = create_prog(pick(gles, &as_vs_gl, &as_vs_es), pick(gles, &as_fs_gl, &as_fs_es), false);
    s->post_loc_pos = 0;
    s->post_loc_texCoord = 1;
    s->post_loc_proj = glGetUniformLocation(s->post_program, "u_projection");
    s->post_loc_sampler = glGetUniformLocation(s->post_program, "u_texture");
    s->post_loc_tsize = glGetUniformLocation(s->post_program, "u_texelSize");
    s->post_loc_rotation = glGetUniformLocation(s->post_program, "u_rotation");
    s->post_loc_center = glGetUniformLocation(s->post_program, "u_center");
    s->post_loc_crt = glGetUniformLocation(s->post_program, "u_crt");
    s->post_loc_time = glGetUniformLocation(s->post_program, "u_time");
}

void terminate_all_shaders(void) {
    GLBackendState *s = geogl_get_state();
    glDeleteProgram(s->draw_program);
    glDeleteProgram(s->video_program);
    glDeleteProgram(s->post_program);
}
