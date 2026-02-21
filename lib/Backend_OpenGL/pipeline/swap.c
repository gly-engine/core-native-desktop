#include <stdlib.h>
#include <string.h>

#include "gefilter.h"
#include "geopengl.h"

void ge_pipeline_resize(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();
    s->window_width = w;
    s->window_height = h;
}

void ge_pipeline_start(void) {
    GLBackendState *s = geogl_get_state();
    
    // Direct to screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, s->window_width, s->window_height);
    
    glClearColor(s->clear_color[0], s->clear_color[1], s->clear_color[2], s->clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0, -1, 1);
    
    s->batch_count = 0;
}

void ge_pipeline_flush(void) {
    ge_pipeline_flush_primitives();
}
