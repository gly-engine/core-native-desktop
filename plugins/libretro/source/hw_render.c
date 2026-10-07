#include <stdio.h>
#include <string.h>
#include "libretro.h"
#include "hw_render.h"
#include "main.h"

static struct {
    uintptr_t            (*fbo_get)(void);
    void                 (*fbo_ensure)(int, int);
    void                 (*fbo_destroy)(void);
    void                 (*set_active)(bool, bool);
    retro_proc_address_t (*proc_address)(const char *);
    void                 (*restore_context)(void);
} hw;

static bool hw_bind(void) {
    if (hw.fbo_ensure) return true;
    api->registry("get", "function:ge_hw_fbo_get",         (void *)&hw.fbo_get,         NULL);
    api->registry("get", "function:ge_hw_fbo_ensure",      (void *)&hw.fbo_ensure,      NULL);
    api->registry("get", "function:ge_hw_fbo_destroy",     (void *)&hw.fbo_destroy,     NULL);
    api->registry("get", "function:ge_hw_set_active",      (void *)&hw.set_active,      NULL);
    api->registry("get", "function:ge_hw_proc_address",    (void *)&hw.proc_address,    NULL);
    api->registry("get", "function:ge_hw_restore_context", (void *)&hw.restore_context, NULL);
    return hw.fbo_ensure != NULL;
}

static struct retro_hw_render_callback s_cb = {0};
static bool s_active   = false;
static int  s_pending_w = 0, s_pending_h = 0;

// ---------------------------------------------------------------------------
// Callbacks given to the core
// ---------------------------------------------------------------------------
static uintptr_t RETRO_CALLCONV cb_get_current_framebuffer(void) {
    hw_bind();
    return hw.fbo_get ? hw.fbo_get() : 0;
}

static retro_proc_address_t RETRO_CALLCONV cb_get_proc_address(const char *sym) {
    hw_bind();
    return hw.proc_address ? hw.proc_address(sym) : NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool libretro_hw_handle_env(unsigned cmd, void *data) {
    switch (cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL) {
        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            struct retro_hw_render_callback *cb = (struct retro_hw_render_callback *)data;
            if (!cb) return false;

            // Accept OpenGL and GLES 2/3; reject Vulkan/D3D/etc.
            switch (cb->context_type) {
                case RETRO_HW_CONTEXT_OPENGL:
                case RETRO_HW_CONTEXT_OPENGL_CORE:
                case RETRO_HW_CONTEXT_OPENGLES2:
                case RETRO_HW_CONTEXT_OPENGLES3:
                case RETRO_HW_CONTEXT_OPENGLES_VERSION:
                    break;
                default:
                    fprintf(stderr, "Libretro HW: context type %d not supported\n", cb->context_type);
                    return false;
            }

            s_cb = *cb;
            s_cb.get_current_framebuffer = cb_get_current_framebuffer;
            s_cb.get_proc_address        = cb_get_proc_address;
            *cb = s_cb; // write back so the core sees our callbacks

            fprintf(stderr, "Libretro HW: SET_HW_RENDER aceito (type=%d depth=%d stencil=%d)\n",
                    s_cb.context_type, s_cb.depth, s_cb.stencil);
            s_active = true;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
            // Let the core know we prefer GLES2 or desktop GL.
            // ge_hw_fbo_get() returning 0 when no backend = weak stub path.
            // We always prefer OPENGLES2 as the lowest common denominator;
            // the Backend_OpenGL strong symbol can override this if needed.
            if (data) *(unsigned *)data = RETRO_HW_CONTEXT_OPENGLES2;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
            // Accept but ignore – we don't do Vulkan negotiation.
            return true;

        default:
            return false;
    }
}

void libretro_hw_context_reset(int w, int h) {
    if (!s_active) return;
    // Don't touch GL here: this is called from retro_load_game which may run
    // before the GL context / GLAD pointers are initialized (gecnd_set_args path).
    // Store dimensions and let libretro_hw_gl_ready() finish when GL is up.
    s_pending_w = w;
    s_pending_h = h;
    fprintf(stderr, "Libretro HW: context_reset pendente (%dx%d), aguardando GL\n", w, h);
}

void libretro_hw_gl_ready(void) {
    if (!s_active || s_pending_w == 0) return;
    if (!hw_bind()) return;
    hw.fbo_ensure(s_pending_w, s_pending_h);
    hw.set_active(false, s_cb.bottom_left_origin);
    if (s_cb.context_reset) {
        fprintf(stderr, "Libretro HW: context_reset (%dx%d)\n", s_pending_w, s_pending_h);
        s_cb.context_reset();
    }
    s_pending_w = s_pending_h = 0;
}

void libretro_hw_context_destroy(void) {
    if (!s_active) return;
    if (s_cb.context_destroy) s_cb.context_destroy();
}

bool libretro_hw_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    (void)pitch;
    if (!s_active) return false;
    if (data != RETRO_HW_FRAME_BUFFER_VALID) return false;

    if (hw.set_active) hw.set_active(true, s_cb.bottom_left_origin);
    (void)width; (void)height; // display backend reads size from FBO directly
    return true;
}

bool libretro_hw_is_active(void) {
    return s_active;
}

void libretro_hw_restore_context(void) {
    if (hw.restore_context) hw.restore_context();
}

void libretro_hw_cleanup(void) {
    libretro_hw_context_destroy();
    if (hw.fbo_destroy) hw.fbo_destroy();
    if (hw.set_active)  hw.set_active(false, false);
    memset(&s_cb, 0, sizeof(s_cb));
    s_active    = false;
    s_pending_w = s_pending_h = 0;
}
