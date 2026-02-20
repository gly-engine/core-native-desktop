#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "gemedia.h"
#include "gecnd.h"

static int pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static bool core_initialized = false;

static void core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!data) return;

    int ge_fmt = (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? GE_PIX_FMT_RGBA8888 : GE_PIX_FMT_RGB565;
    gecnd_buffer_resize(width, height, ge_fmt);
    
    MediaFrame *f = gecnd_buffer_get_back();
    int bpp = (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? 4 : 2;

    if (pitch == (size_t)f->linesize[0]) {
        memcpy(f->data[0], data, height * pitch);
    } else {
        for (unsigned y = 0; y < height; y++) {
            memcpy(f->data[0] + y * f->linesize[0], 
                   (const uint8_t*)data + y * pitch, width * bpp);
        }
    }
    
    atomic_store(&f->ready, true);
    gecnd_buffer_swap();
}

static void core_audio_sample(int16_t left, int16_t right) { (void)left; (void)right; }
static size_t core_audio_sample_batch(const int16_t *data, size_t frames) { (void)data; return frames; }
static void core_input_poll(void) {}

extern int16_t engine_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id);

static bool core_environment(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: pixel_format = *(int*)data; return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "."; return true;
        default: return false;
    }
}

void libretro_init_core(void) {
    if (core_initialized) return;
    retro_set_environment(core_environment);
    retro_set_video_refresh(core_video_refresh);
    retro_set_audio_sample(core_audio_sample);
    retro_set_audio_sample_batch(core_audio_sample_batch);
    retro_set_input_poll(core_input_poll);
    retro_set_input_state(engine_input_state_cb);
    retro_init();
    core_initialized = true;
}

void libretro_deinit_core(void) {
    if (!core_initialized) return;
    retro_unload_game();
    retro_deinit();
    core_initialized = false;
}

MediaFrame* libretro_get_frame(void) {
    MediaFrame *f = gecnd_buffer_get_front();
    if (atomic_load(&f->ready)) return f;
    return NULL;
}

void libretro_run_frame(void) {
    if (core_initialized) retro_run();
}
