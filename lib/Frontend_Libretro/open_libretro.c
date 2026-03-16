#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <unistd.h>
#include "libretro.h"
#include "gemedia.h"
#include "gecnd.h"
#include "gebuffer.h"
#include "gedll.h"
#include "gehook.h"
#include "buffer.h"

static int pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static bool core_initialized = false;
static bool core_init_done = false;
static LIB_HANDLE core_handle = NULL;
static char system_dir[1024] = ".";

// Libretro API function pointers
static void (*p_retro_set_environment)(retro_environment_t) = NULL;
static void (*p_retro_set_video_refresh)(retro_video_refresh_t) = NULL;
static void (*p_retro_set_audio_sample)(retro_audio_sample_t) = NULL;
static void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;
static void (*p_retro_set_input_poll)(retro_input_poll_t) = NULL;
static void (*p_retro_set_input_state)(retro_input_state_t) = NULL;
static void (*p_retro_init)(void) = NULL;
static void (*p_retro_deinit)(void) = NULL;
static unsigned (*p_retro_api_version)(void) = NULL;
static void (*p_retro_get_system_info)(struct retro_system_info*) = NULL;
static void (*p_retro_get_system_av_info)(struct retro_system_av_info*) = NULL;
static void (*p_retro_set_controller_port_device)(unsigned, unsigned) = NULL;
static void (*p_retro_reset)(void) = NULL;
static void (*p_retro_run)(void) = NULL;
static bool (*p_retro_load_game)(const struct retro_game_info*) = NULL;
static void (*p_retro_unload_game)(void) = NULL;

static void reset_pointers(void) {
    p_retro_set_environment = NULL;
    p_retro_set_video_refresh = NULL;
    p_retro_set_audio_sample = NULL;
    p_retro_set_audio_sample_batch = NULL;
    p_retro_set_input_poll = NULL;
    p_retro_set_input_state = NULL;
    p_retro_init = NULL;
    p_retro_deinit = NULL;
    p_retro_api_version = NULL;
    p_retro_get_system_info = NULL;
    p_retro_get_system_av_info = NULL;
    p_retro_set_controller_port_device = NULL;
    p_retro_reset = NULL;
    p_retro_run = NULL;
    p_retro_load_game = NULL;
    p_retro_unload_game = NULL;
}

static void RETRO_CALLCONV core_log(enum retro_log_level level, const char *fmt, ...) {
    const char *levels[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    if (level < 0 || level > 3) level = RETRO_LOG_INFO;
    fprintf(stderr, "[Libretro %s] ", levels[level]);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static void RETRO_CALLCONV core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    // data == NULL: core requests frame dupe (GET_CAN_DUPE=true) — keep last frame
    if (!data) return;

    int ge_fmt = (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? GECND_PIX_FMT_RGBA8888 : GECND_PIX_FMT_RGB565;
    gecnd_buffer_resize((int)width, (int)height, ge_fmt);

    MediaFrame *f = gecnd_buffer_get_back();
    if (!f) return;

    if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
        // BGRX -> RGBA conversion (SIMD: NEON / SSSE3 / scalar)
        libretro_copy_xrgb8888(f->data[0], f->linesize[0],
                               (const uint8_t *)data, (int)pitch,
                               (int)width, (int)height);
    } else {
        libretro_copy_rgb565(f->data[0], f->linesize[0],
                             (const uint8_t *)data, (int)pitch,
                             (int)width, (int)height);
    }

    atomic_store(&f->ready, true);
    gecnd_buffer_swap();
}

static void RETRO_CALLCONV core_audio_sample(int16_t left, int16_t right) { (void)left; (void)right; }
static size_t RETRO_CALLCONV core_audio_sample_batch(const int16_t *data, size_t frames) { (void)data; return frames; }
static void RETRO_CALLCONV core_input_poll(void) {}

extern int16_t RETRO_CALLCONV engine_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id);

static bool core_environment(unsigned cmd, void *data) {
    switch (cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL) {
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
            if (data) *(const char**)data = (system_dir[0] ? system_dir : ".");
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            if (data) {
                struct retro_log_callback *cb = (struct retro_log_callback*)data;
                cb->log = core_log;
            }
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
            return false;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data) *(bool*)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            if (data) *(unsigned*)data = 0; // English
            return true;
        case RETRO_ENVIRONMENT_GET_USERNAME:
            if (data) *(const char**)data = "gecnd";
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
            if (data) *(uint64_t*)data = (1ULL << RETRO_DEVICE_JOYPAD);
            return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            if (data) *(unsigned*)data = 1;
            return true;
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
            if (data) {
                struct retro_game_geometry *geom = (struct retro_game_geometry*)data;
                int ge_fmt = (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? GECND_PIX_FMT_RGBA8888 : GECND_PIX_FMT_RGB565;
                gecnd_buffer_resize((int)geom->base_width, (int)geom->base_height, ge_fmt);
            }
            return true;
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            return true;
        default:
            return false;
    }
}

#define LOAD_SYM_MANDATORY(name) \
    *(void**)(&p_##name) = get_symbol(core_handle, #name); \
    if (!p_##name) { fprintf(stderr, "Libretro: failed to load mandatory symbol %s\n", #name); return false; }

#define LOAD_SYM_OPTIONAL(name) \
    *(void**)(&p_##name) = get_symbol(core_handle, #name);

bool native_libretro_load(const char *path) {
    if (core_handle) close_library(core_handle);
    core_handle = NULL;
    reset_pointers();
    
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

    LOAD_SYM_MANDATORY(retro_set_environment);
    LOAD_SYM_MANDATORY(retro_set_video_refresh);
    LOAD_SYM_MANDATORY(retro_set_audio_sample);
    LOAD_SYM_MANDATORY(retro_set_audio_sample_batch);
    LOAD_SYM_MANDATORY(retro_set_input_poll);
    LOAD_SYM_MANDATORY(retro_set_input_state);
    LOAD_SYM_MANDATORY(retro_init);
    LOAD_SYM_MANDATORY(retro_deinit);
    LOAD_SYM_MANDATORY(retro_api_version);
    LOAD_SYM_MANDATORY(retro_get_system_info);
    LOAD_SYM_MANDATORY(retro_get_system_av_info);
    LOAD_SYM_MANDATORY(retro_run);
    LOAD_SYM_MANDATORY(retro_load_game);
    LOAD_SYM_MANDATORY(retro_unload_game);

    LOAD_SYM_OPTIONAL(retro_set_controller_port_device);
    LOAD_SYM_OPTIONAL(retro_reset);

    if (p_retro_set_environment) p_retro_set_environment(core_environment);
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
    if (p_retro_get_system_info) {
        printf("Libretro: calling retro_get_system_info\n");
        p_retro_get_system_info(&sys_info);
    }

    struct retro_game_info info = {0};
    info.path = full_path;
    info.meta = NULL;
    
    FILE *f = fopen(full_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        info.size = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        void *data = malloc(info.size);
        if (data) {
            if (fread(data, 1, info.size, f) != info.size) {
                free(data);
                data = NULL;
            } else info.data = data;
        }
        fclose(f);
    } else {
        fprintf(stderr, "Libretro: failed to open game: %s\n", full_path);
        return false;
    }

    if (p_retro_set_video_refresh) p_retro_set_video_refresh(core_video_refresh);
    if (p_retro_set_audio_sample) p_retro_set_audio_sample(core_audio_sample);
    if (p_retro_set_audio_sample_batch) p_retro_set_audio_sample_batch(core_audio_sample_batch);
    if (p_retro_set_input_poll) p_retro_set_input_poll(core_input_poll);
    if (p_retro_set_input_state) p_retro_set_input_state(engine_input_state_cb);

    if (!core_init_done) {
        if (p_retro_init) {
            printf("Libretro: calling retro_init\n");
            p_retro_init();
        }
        core_init_done = true;
    }
    
    if (p_retro_set_controller_port_device) p_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    
    printf("Libretro: Loading game: %s\n", full_path);
    bool ok = false;
    if (p_retro_load_game) ok = p_retro_load_game(&info);
    
    if (info.data) free((void*)info.data);

    if (ok) {
        struct retro_system_av_info av_info = {0};
        if (p_retro_get_system_av_info) p_retro_get_system_av_info(&av_info);
        
        int ge_fmt = (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? GECND_PIX_FMT_RGBA8888 : GECND_PIX_FMT_RGB565;
        if (av_info.geometry.base_width > 0 && av_info.geometry.base_height > 0) {
            gecnd_buffer_resize((int)av_info.geometry.base_width, (int)av_info.geometry.base_height, ge_fmt);
        }

        core_initialized = true;
        return true;
    }
    return false;
}

void libretro_init_core(void) {
}

void libretro_deinit_core(void) {
    if (core_initialized) {
        if (p_retro_unload_game) p_retro_unload_game();
    }
    if (core_init_done) {
        if (p_retro_deinit) p_retro_deinit();
    }
    if (core_handle) close_library(core_handle);
    core_initialized = core_init_done = false;
    core_handle = NULL;
    reset_pointers();
}

MediaFrame* libretro_get_frame(void) {
    return gecnd_get_background_frame();
}

void libretro_run_frame(void) {
    if (core_initialized && p_retro_run) p_retro_run();
}

bool libretro_is_running(void) {
    return core_initialized;
}

// Libretro dummy stubs (some cores might expect these if they link against the frontend)
RETRO_API void retro_init(void) { if (p_retro_init) p_retro_init(); }
RETRO_API void retro_deinit(void) { if (p_retro_deinit) p_retro_deinit(); }
RETRO_API unsigned retro_api_version(void) { return p_retro_api_version ? p_retro_api_version() : 0; }
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { if (p_retro_set_controller_port_device) p_retro_set_controller_port_device(port, device); }
RETRO_API void retro_get_system_info(struct retro_system_info *info) { if (p_retro_get_system_info) p_retro_get_system_info(info); }
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) { if (p_retro_get_system_av_info) p_retro_get_system_av_info(info); }
RETRO_API void retro_set_environment(retro_environment_t cb) { if (p_retro_set_environment) p_retro_set_environment(cb); }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { if (p_retro_set_audio_sample) p_retro_set_audio_sample(cb); }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { if (p_retro_set_audio_sample_batch) p_retro_set_audio_sample_batch(cb); }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { if (p_retro_set_input_poll) p_retro_set_input_poll(cb); }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { if (p_retro_set_input_state) p_retro_set_input_state(cb); }
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { if (p_retro_set_video_refresh) p_retro_set_video_refresh(cb); }
RETRO_API void retro_reset(void) { if (p_retro_reset) p_retro_reset(); }
RETRO_API void retro_run(void) { if (p_retro_run) p_retro_run(); }
RETRO_API bool retro_load_game(const struct retro_game_info *info) { return p_retro_load_game ? p_retro_load_game(info) : false; }
RETRO_API void retro_unload_game(void) { if (p_retro_unload_game) p_retro_unload_game(); }
RETRO_API unsigned retro_get_region(void) { return 0; }
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { (void)type; (void)info; (void)num; return false; }
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t len) { (void)data; (void)len; return false; }
RETRO_API bool retro_unserialize(const void *data, size_t len) { (void)data; (void)len; return false; }
RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned idx, bool enabled, const char *code) { (void)idx; (void)enabled; (void)code; }
