#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "geopengl.h"

#include <gecnd/shadder_gl_draw_vert.h>
#include <gecnd/shadder_gl_draw_frag.h>
#include <gecnd/shadder_es_draw_vert.h>
#include <gecnd/shadder_es_draw_frag.h>

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
    
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glBindAttribLocation(p, 2, "a_color");
    glBindAttribLocation(p, 3, "a_local");
    glBindAttribLocation(p, 4, "a_sdf");
    glBindAttribLocation(p, 5, "a_size");

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
    s->draw_program = create_prog(pick(gles, &ds_vs_gl, &ds_vs_es), pick(gles, &ds_fs_gl, &ds_fs_es));
    s->draw_loc_proj = glGetUniformLocation(s->draw_program, "u_mvp");
    s->draw_loc_atlas = glGetUniformLocation(s->draw_program, "u_atlas");
}

void terminate_all_shaders(void) {
    GLBackendState *s = geogl_get_state();
    glDeleteProgram(s->draw_program);
}
