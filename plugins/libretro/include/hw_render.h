#ifndef GECND_LIBRETRO_HW_RENDER_H
#define GECND_LIBRETRO_HW_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "libretro.h"

// Called by open_libretro.c to handle HW render environment commands.
// Returns true if the command was handled.
bool libretro_hw_handle_env(unsigned cmd, void *data);

// Call after retro_load_game() succeeds. Saves the FBO dimensions but does NOT
// touch GL yet — the GL context may not be up at this point.
void libretro_hw_context_reset(int w, int h);

// Called by the GL backend after the context + GLAD are fully initialized.
// Completes the deferred FBO creation and calls the core's context_reset callback.
void libretro_hw_gl_ready(void);

// Call before retro_deinit() / close.
void libretro_hw_context_destroy(void);

// Drop-in replacement for core_video_refresh for the HW path.
// Returns true if it handled the frame (data == RETRO_HW_FRAME_BUFFER_VALID).
bool libretro_hw_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch);

// True when a HW render core is active.
bool libretro_hw_is_active(void);

// Free all HW render state (called from libretro_deinit_core).
void libretro_hw_cleanup(void);

// Restore the default framebuffer and viewport after a HW core's retro_run().
void libretro_hw_restore_context(void);

#endif
