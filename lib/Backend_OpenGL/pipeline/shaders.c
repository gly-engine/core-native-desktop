#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "geopengl.h"

// ES Shaders
#include <gecnd/shadder_es_shape_simple_vert.h>
#include <gecnd/shadder_es_shape_simples_frag.h>
#include <gecnd/shadder_es_shape_complex_vert.h>
#include <gecnd/shadder_es_shape_complex_frag.h>
#include <gecnd/shadder_es_texture_vert.h>
#include <gecnd/shadder_es_texture_frag.h>

// GL Shaders
#include <gecnd/shadder_gl_shape_simple_vert.h>
#include <gecnd/shadder_gl_shape_simple_frag.h>
#include <gecnd/shadder_gl_shape_complex_vert.h>
#include <gecnd/shadder_gl_shape_complex_frag.h>
#include <gecnd/shadder_gl_texture_vert.h>
#include <gecnd/shadder_gl_texture_frag.h>

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

static GLuint create_prog(const shader_src_t* vs, const shader_src_t* fs, GEProgramType type) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs->src, vs->len, vs->name);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs->src, fs->len, fs->name);
    glAttachShader(p, v);
    glAttachShader(p, f);
    
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_color");
    if (type == GE_PROG_COMPLEX) {
        glBindAttribLocation(p, 2, "a_local");
        glBindAttribLocation(p, 3, "a_radius");
        glBindAttribLocation(p, 4, "a_mode");
    } else if (type == GE_PROG_ATLAS) {
        glBindAttribLocation(p, 2, "a_uv");
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

    // Simple Shape
    shader_src_t simple_vs_gl = SHADER(shadder_gl_shape_simple_vert);
    shader_src_t simple_fs_gl = SHADER(shadder_gl_shape_simple_frag);
    shader_src_t simple_vs_es = SHADER(shadder_es_shape_simple_vert);
    shader_src_t simple_fs_es = SHADER(shadder_es_shape_simples_frag);
    s->programs[GE_PROG_SIMPLE].id = create_prog(pick(gles, &simple_vs_gl, &simple_vs_es), pick(gles, &simple_fs_gl, &simple_fs_es), GE_PROG_SIMPLE);
    s->programs[GE_PROG_SIMPLE].loc_proj = glGetUniformLocation(s->programs[GE_PROG_SIMPLE].id, "u_proj");

    // Complex Shape
    shader_src_t complex_vs_gl = SHADER(shadder_gl_shape_complex_vert);
    shader_src_t complex_fs_gl = SHADER(shadder_gl_shape_complex_frag);
    shader_src_t complex_vs_es = SHADER(shadder_es_shape_complex_vert);
    shader_src_t complex_fs_es = SHADER(shadder_es_shape_complex_frag);
    s->programs[GE_PROG_COMPLEX].id = create_prog(pick(gles, &complex_vs_gl, &complex_vs_es), pick(gles, &complex_fs_gl, &complex_fs_es), GE_PROG_COMPLEX);
    s->programs[GE_PROG_COMPLEX].loc_proj = glGetUniformLocation(s->programs[GE_PROG_COMPLEX].id, "u_proj");
    s->programs[GE_PROG_COMPLEX].loc_size = glGetUniformLocation(s->programs[GE_PROG_COMPLEX].id, "u_size");
    s->programs[GE_PROG_COMPLEX].loc_thickness = glGetUniformLocation(s->programs[GE_PROG_COMPLEX].id, "u_thickness");
    s->programs[GE_PROG_COMPLEX].loc_aa_blur = glGetUniformLocation(s->programs[GE_PROG_COMPLEX].id, "u_aa_blur");

    // Atlas Texture
    shader_src_t atlas_vs_gl = SHADER(shadder_gl_texture_vert);
    shader_src_t atlas_fs_gl = SHADER(shadder_gl_texture_frag);
    shader_src_t atlas_vs_es = SHADER(shadder_es_texture_vert);
    shader_src_t atlas_fs_es = SHADER(shadder_es_texture_frag);
    s->programs[GE_PROG_ATLAS].id = create_prog(pick(gles, &atlas_vs_gl, &atlas_vs_es), pick(gles, &atlas_fs_gl, &atlas_fs_es), GE_PROG_ATLAS);
    s->programs[GE_PROG_ATLAS].loc_proj = glGetUniformLocation(s->programs[GE_PROG_ATLAS].id, "u_proj");
    s->programs[GE_PROG_ATLAS].loc_tex = glGetUniformLocation(s->programs[GE_PROG_ATLAS].id, "u_tex");
}

void terminate_all_shaders(void) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < GE_PROG_COUNT; i++) {
        if (s->programs[i].id) glDeleteProgram(s->programs[i].id);
    }
}
