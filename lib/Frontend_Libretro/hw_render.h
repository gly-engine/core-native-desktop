#ifndef GECND_LIBRETRO_HW_RENDER_H
#define GECND_LIBRETRO_HW_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "libretro.h"

// Called by open_libretro.c to handle HW render environment commands.
// Returns true if the command was handled.
bool libretro_hw_handle_env(unsigned cmd, void *data);

// Call after retro_load_game() succeeds and the GL context is ready.
// w/h: max framebuffer size the core will render at.
void libretro_hw_context_reset(int w, int h);

// Call before retro_deinit() / close.
void libretro_hw_context_destroy(void);

// Drop-in replacement for core_video_refresh for the HW path.
// Returns true if it handled the frame (data == RETRO_HW_FRAME_BUFFER_VALID).
bool libretro_hw_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch);

// True when a HW render core is active.
bool libretro_hw_is_active(void);

// Free all HW render state (called from libretro_deinit_core).
void libretro_hw_cleanup(void);

// ---------------------------------------------------------------------------
// Weak interface – Backend_OpenGL provides strong implementations.
// Frontend_Libretro defines the weak stubs so the code links without GL too.
// ---------------------------------------------------------------------------

// Return the FBO id the core should render into.
uintptr_t ge_hw_fbo_get(void);

// Create or resize the HW FBO to w x h (must be called on the GL thread).
void ge_hw_fbo_ensure(int w, int h);

// Delete the HW FBO resources.
void ge_hw_fbo_destroy(void);

// Signal the display backend that a HW frame is ready.
// active=false clears HW mode; bottom_left_origin: OpenGL UV convention.
void ge_hw_set_active(bool active, bool bottom_left_origin);

// Resolve a GL symbol name (wraps platform_get_proc_address).
retro_proc_address_t ge_hw_proc_address(const char *sym);

// Restore the default framebuffer and viewport after a HW core's retro_run().
void ge_hw_restore_context(void);

#endif
