#include <stdlib.h>
#include <string.h>
#include "gecnd.h"
#include "geopengl.h"

void ge_pipeline_init(uint16_t w, uint16_t h) {
    GLBackendState *s = geogl_get_state();

    s->window_width = w;
    s->window_height = h;
    
    if (s->post_fbo_texture) glDeleteTextures(1, &s->post_fbo_texture);
    if (s->post_fbo) glDeleteFramebuffers(1, &s->post_fbo);
    glGenTextures(1, &s->post_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, s->post_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &s->post_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->post_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->post_fbo_texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
