#include <stdio.h>
#include "geopengl.h"

// ---------------------------------------------------------------------------
// Strong implementations of the ge_hw_* weak interface declared in
// lib/Frontend_Libretro/hw_render.h.  These replace the weak stubs when the
// Backend_OpenGL is linked, giving Frontend_Libretro GL acceleration without
// it needing to include any GL headers.
// ---------------------------------------------------------------------------

uintptr_t ge_hw_fbo_get(void) {
    return (uintptr_t)geogl_get_state()->hw_fbo_id;
}

void ge_hw_fbo_ensure(int w, int h) {
    GLBackendState *s = geogl_get_state();
    if (s->hw_fbo_id && s->hw_fbo_width == w && s->hw_fbo_height == h) return;

    if (s->hw_fbo_id) {
        glDeleteFramebuffers(1, &s->hw_fbo_id);
        glDeleteTextures(1, &s->hw_fbo_tex);
        if (s->hw_fbo_depth_rb) glDeleteRenderbuffers(1, &s->hw_fbo_depth_rb);
        s->hw_fbo_id = s->hw_fbo_tex = s->hw_fbo_depth_rb = 0;
    }

    s->hw_fbo_width  = w;
    s->hw_fbo_height = h;

    // Color texture
    glGenTextures(1, &s->hw_fbo_tex);
    glBindTexture(GL_TEXTURE_2D, s->hw_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth renderbuffer (cores like PCSX ReARMed need depth)
    glGenRenderbuffers(1, &s->hw_fbo_depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, s->hw_fbo_depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // FBO assembly
    glGenFramebuffers(1, &s->hw_fbo_id);
    glBindFramebuffer(GL_FRAMEBUFFER, s->hw_fbo_id);
    glFramebufferTexture2D(GL_FRAMEBUFFER,    GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,   s->hw_fbo_tex,      0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_RENDERBUFFER, s->hw_fbo_depth_rb);

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "Backend_OpenGL HW FBO: incompleto (0x%x)\n", st);
    else
        fprintf(stderr, "Backend_OpenGL HW FBO: OK %dx%d (tex=%u fbo=%u)\n",
                w, h, s->hw_fbo_tex, s->hw_fbo_id);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ge_hw_fbo_destroy(void) {
    GLBackendState *s = geogl_get_state();
    if (s->hw_fbo_id)       glDeleteFramebuffers(1,  &s->hw_fbo_id);
    if (s->hw_fbo_tex)      glDeleteTextures(1,       &s->hw_fbo_tex);
    if (s->hw_fbo_depth_rb) glDeleteRenderbuffers(1,  &s->hw_fbo_depth_rb);
    s->hw_fbo_id = s->hw_fbo_tex = s->hw_fbo_depth_rb = 0;
    s->hw_fbo_width = s->hw_fbo_height = 0;
}

void ge_hw_set_active(bool active, bool bottom_left_origin) {
    GLBackendState *s = geogl_get_state();
    s->hw_render_active      = active;
    s->hw_bottom_left_origin = bottom_left_origin;
}

/**
 * @todo move?
 */
void* ge_hw_proc_address(const char *sym) {
    return (void*) platform_get_proc_address(sym);
}

void ge_hw_restore_context(void) {
    GLBackendState *s = geogl_get_state();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, s->window_width, s->window_height);
}
