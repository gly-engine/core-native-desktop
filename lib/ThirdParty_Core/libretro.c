#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#include "libretro.h"
#include "gemedia.h"
#include "gecnd.h"
#include "gebuffer.h"
#include "gedll.h"
#include "gehook.h"

static int pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static bool core_initialized = false;
static bool core_init_done = false;
static LIB_HANDLE core_handle = NULL;
static char system_dir[1024] = ".";

// Libretro API function pointers
static void (*p_retro_set_environment)(retro_environment_t);
static void (*p_retro_set_video_refresh)(retro_video_refresh_t);
static void (*p_retro_set_audio_sample)(retro_audio_sample_t);
static void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*p_retro_set_input_poll)(retro_input_poll_t);
static void (*p_retro_set_input_state)(retro_input_state_t);
static void (*p_retro_init)(void);
static void (*p_retro_deinit)(void);
static unsigned (*p_retro_api_version)(void);
static void (*p_retro_get_system_info)(struct retro_system_info*);
static void (*p_retro_get_system_av_info)(struct retro_system_av_info*);
static void (*p_retro_set_controller_port_device)(unsigned, unsigned);
static void (*p_retro_reset)(void);
static void (*p_retro_run)(void);
static bool (*p_retro_load_game)(const struct retro_game_info*);
static void (*p_retro_unload_game)(void);

static void core_log(int level, const char *fmt, ...) {
    const char *levels[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    if (level < 0 || level > 3) level = 1;
    fprintf(stderr, "[Libretro %s] ", levels[level]);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static void core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!data) return;

    int ge_fmt = (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? GECND_PIX_FMT_RGBA8888 : GECND_PIX_FMT_RGB565;
    gecnd_buffer_resize(width, height, ge_fmt);
    
    MediaFrame *f = gecnd_buffer_get_back();
    if (!f) return;
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
    // printf("Libretro: Env cmd %u\n", cmd);
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *(bool*)data = true;
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            if (data) {
                pixel_format = *(int*)data;
                core_log(RETRO_LOG_INFO, "Env: Pixel Format set to %d\n", pixel_format);
            }
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            if (data) *(const char**)data = system_dir;
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            if (data) {
                struct retro_log_callback *cb = (struct retro_log_callback*)data;
                cb->log = core_log;
            }
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
            if (data) {
                struct retro_variable *var = (struct retro_variable*)data;
                var->value = NULL; // No specific variables set
                return true;
            }
            return false;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            if (data) *(unsigned*)data = 0; // English
            return true;
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
            if (data) *(uint64_t*)data = (1ULL << RETRO_DEVICE_JOYPAD);
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data) *(bool*)data = false;
            return true;
        default:
            return false;
    }
}

#define LOAD_SYM(name) \
    p_##name = (void*)get_symbol(core_handle, #name); \
    if (!p_##name) { fprintf(stderr, "Libretro: failed to load symbol %s\n", #name); return false; }

bool native_libretro_load(const char *path) {
    if (core_handle) close_library(core_handle);
    
    char full_path[1024];
    char exe_dir[512];
    gecnd_utils_get_exe_cwd(exe_dir, sizeof(exe_dir));
    
    if (exe_dir[0] == '/') {
        strncpy(system_dir, exe_dir, sizeof(system_dir));
    } else {
        getcwd(system_dir, sizeof(system_dir));
    }

    const char* variations[] = {
        path,
        "%s/%s",
        "%s/%s_libretro.so",
        "%s/lib%s_libretro.so",
        "%s_libretro.so"
    };

    core_handle = NULL;
    for (size_t i = 0; i < sizeof(variations)/sizeof(variations[0]); i++) {
        if (i == 0 || i == 4) snprintf(full_path, sizeof(full_path), variations[i], path);
        else snprintf(full_path, sizeof(full_path), variations[i], exe_dir, path);

        core_handle = load_library(full_path);
        if (core_handle) {
            printf("Libretro: loaded core from %s\n", full_path);
            break;
        }
    }

    if (!core_handle) return false;

    LOAD_SYM(retro_set_environment);
    LOAD_SYM(retro_set_video_refresh);
    LOAD_SYM(retro_set_audio_sample);
    LOAD_SYM(retro_set_audio_sample_batch);
    LOAD_SYM(retro_set_input_poll);
    LOAD_SYM(retro_set_input_state);
    LOAD_SYM(retro_init);
    LOAD_SYM(retro_deinit);
    LOAD_SYM(retro_api_version);
    LOAD_SYM(retro_get_system_info);
    LOAD_SYM(retro_get_system_av_info);
    LOAD_SYM(retro_set_controller_port_device);
    LOAD_SYM(retro_reset);
    LOAD_SYM(retro_run);
    LOAD_SYM(retro_load_game);
    LOAD_SYM(retro_unload_game);

    p_retro_set_environment(core_environment);
    return true;
}

bool native_libretro_game(const char *path) {
    if (!core_handle) return false;

    char full_path[1024];
    if (path[0] != '/' && path[0] != '.' && !(path[0] != '\0' && path[1] == ':')) {
        char exe_dir[512];
        gecnd_utils_get_exe_cwd(exe_dir, sizeof(exe_dir));
        snprintf(full_path, sizeof(full_path), "%s/%s", exe_dir, path);
    } else {
        strncpy(full_path, path, sizeof(full_path));
    }

    struct retro_system_info sys_info = {0};
    p_retro_get_system_info(&sys_info);

    struct retro_game_info info = {0};
    info.path = full_path;
    info.meta = "";
    
    FILE *f = fopen(full_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        info.size = ftell(f);
        fseek(f, 0, SEEK_SET);
        void *data = malloc(info.size);
        if (data) {
            if (fread(data, 1, info.size, f) != info.size) {
                free(data);
                data = NULL;
            } else info.data = data;
        }
        fclose(f);
    } else return false;

    // MANDATORY: Setup callbacks BEFORE init
    p_retro_set_video_refresh(core_video_refresh);
    p_retro_set_audio_sample(core_audio_sample);
    p_retro_set_audio_sample_batch(core_audio_sample_batch);
    p_retro_set_input_poll(core_input_poll);
    p_retro_set_input_state(engine_input_state_cb);

    if (!core_init_done) {
        p_retro_init();
        core_init_done = true;
    }
    
    p_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    
    printf("Libretro: Loading game: %s\n", full_path);
    bool ok = p_retro_load_game(&info);
    
    if (info.data) free((void*)info.data);

    if (ok) {
        core_initialized = true;
        return true;
    }
    return false;
}

void libretro_init_core(void) {}

void libretro_deinit_core(void) {
    if (core_initialized) p_retro_unload_game();
    if (core_init_done) p_retro_deinit();
    if (core_handle) close_library(core_handle);
    core_initialized = core_init_done = false;
    core_handle = NULL;
}

MediaFrame* libretro_get_frame(void) {
    MediaFrame *f = gecnd_buffer_get_front();
    if (f && atomic_load(&f->ready)) return f;
    return NULL;
}

void libretro_run_frame(void) {
    if (core_initialized) p_retro_run();
}

bool libretro_is_running(void) {
    return core_initialized;
}
