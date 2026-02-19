#ifndef GEOPENGL_PIPELINE_INTERNAL_H
#define GEOPENGL_PIPELINE_INTERNAL_H

#include <glad/gles2.h>
#include "pipeline.h"

typedef struct {
    GLuint fbo;
    GLuint texture;
    int w, h;
    
    GLuint program;
    GLint loc_pos;
    GLint loc_texCoord;
    GLint loc_proj;
    GLint loc_sampler;

    GLint loc_brightness;
    GLint loc_contrast;
    GLint loc_saturation;
    GLint loc_film_grain;
    GLint loc_aa;
    GLint loc_texel_size;

    GLuint vbo;
} ge_pipeline_state;

ge_pipeline_state* ge_pipeline_get_state(void);

#endif
