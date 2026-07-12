#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <unistd.h>
#include "libretro.h"
#include "gemedia.h"
#include "gecnd.h"
#include "gedll.h"

#include "hw_render.h"
#include "main.h"

const char *scanner_resolve_core(const char *name);
const char *scanner_resolve_rom(const char *name);

static int pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static bool core_initialized = false;
static bool core_init_done = false;
static LIB_HANDLE core_handle = NULL;
static char system_dir[1024] = ".";
static char s_error[256]     = "";

/* ROM temporária gravada em /tmp quando o core exige need_fullpath;
 * removida no deinit (cores de CD podem ler o arquivo depois do load). */
static char s_tmp_rom_path[512] = "";

/* Pending finalize state — populated by *_load_only no worker thread,
 * consumido por native_libretro_game_finalize no main thread. */
static bool                       s_pending_finalize = false;
static struct retro_system_av_info s_pending_av_info;

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

static struct {
    typeof(gamely_daemon_media_background_claim)         *claim;
    typeof(gamely_daemon_media_background_release)       *release;
    typeof(gamely_daemon_media_background_push_xrgb8888) *push_xrgb8888;
    typeof(gamely_daemon_media_background_push_rgb565)   *push_rgb565;
    typeof(gamely_daemon_media_background_get_frame)     *get_frame;
    typeof(gamely_daemon_media_audio_configure)          *audio_configure;
    typeof(gamely_daemon_media_audio_push)               *audio_push;
} media;

static bool media_bind(void) {
    if (media.claim) return true;
    api->registry("get", "function:gamely_daemon_media_background_claim",         (void *)&media.claim,           NULL);
    api->registry("get", "function:gamely_daemon_media_background_release",       (void *)&media.release,         NULL);
    api->registry("get", "function:gamely_daemon_media_background_push_xrgb8888", (void *)&media.push_xrgb8888,   NULL);
    api->registry("get", "function:gamely_daemon_media_background_push_rgb565",   (void *)&media.push_rgb565,     NULL);
    api->registry("get", "function:gamely_daemon_media_background_get_frame",     (void *)&media.get_frame,       NULL);
    api->registry("get", "function:gamely_daemon_media_audio_configure",         (void *)&media.audio_configure, NULL);
    api->registry("get", "function:gamely_daemon_media_audio_push",              (void *)&media.audio_push,      NULL);
    return media.claim != NULL;
}

static void RETRO_CALLCONV core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!data) return;
    if (libretro_hw_video_refresh(data, width, height, pitch)) return;
    if (!media_bind()) return;
    if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
        media.push_xrgb8888((const uint8_t *)data, (int)width, (int)height, (int)pitch);
    } else {
        media.push_rgb565((const uint8_t *)data, (int)width, (int)height, (int)pitch);
    }
}

static void RETRO_CALLCONV core_audio_sample(int16_t left, int16_t right) {
    int16_t buf[2] = { left, right };
    /* audio vem de outro serviço que o claim — pode não estar registrado */
    if (media_bind() && media.audio_push) media.audio_push(buf, 1);
}

static size_t RETRO_CALLCONV core_audio_sample_batch(const int16_t *data, size_t frames) {
    if (media_bind() && media.audio_push) media.audio_push(data, frames);
    return frames;
}

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
            if (data) {
                struct retro_variable *var = (struct retro_variable*)data;
                var->value = url_env_get(var->key);
                core_log(RETRO_LOG_INFO, "GET_VARIABLE: %s = %s\n", var->key, var->value ? var->value : "(not set)");
                return var->value != NULL;
            }
            return false;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data) *(bool*)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            if (data) *(unsigned*)data = 0;
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
        case RETRO_ENVIRONMENT_SET_HW_RENDER:
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
            return libretro_hw_handle_env(cmd, data);
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
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

bool native_libretro_load(const char *path);
bool native_libretro_game(const char *path);
bool native_libretro_game_load_only(const char *path);
bool native_libretro_game_from_buffer(const uint8_t *data, size_t size, const char *name);
void native_libretro_game_finalize(void);
static void libretro_deinit_core(void);

const char *native_libretro_error(void) {
    return s_error;
}

void native_libretro_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_error, sizeof(s_error), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[libretro] %s\n", s_error);
}

bool native_libretro_url(const char *url) {
    char buf[2048];
    strncpy(buf, url, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *sep = strstr(buf, "://");
    if (!sep) {
        native_libretro_set_error("invalid url, expected: core://rom[?key=val&...]");
        return false;
    }
    *sep = '\0';
    char *core = buf;
    char *rom  = sep + 3;

    char *query = strchr(rom, '?');
    if (query) *query++ = '\0';

    url_env_set(url);

    const char *resolved_core = scanner_resolve_core(core);
    if (!resolved_core) {
        native_libretro_set_error("core not found: %s", core);
        return false;
    }
    if (!native_libretro_load(resolved_core)) {
        native_libretro_set_error("failed to open core: %s", resolved_core);
        return false;
    }

    const char *resolved_rom = scanner_resolve_rom(rom);
    if (!resolved_rom) {
        native_libretro_set_error("rom not found: %s", rom);
        return false;
    }
    if (!native_libretro_game(resolved_rom)) {
        native_libretro_set_error("failed to load rom: %s", resolved_rom);
        return false;
    }

    return true;
}

static typeof(gecnd_utils_get_exe_cwd) *fn_exe_cwd;

static void exe_cwd(char *buf, size_t cap) {
    if (!fn_exe_cwd)
        api->registry("get", "function:gecnd_utils_get_exe_cwd", (void *)&fn_exe_cwd, NULL);
    if (fn_exe_cwd) fn_exe_cwd(buf, cap);
    else buf[0] = '\0';
}

bool native_libretro_load(const char *path) {
    if (core_handle) close_library(core_handle);
    core_handle = NULL;
    reset_pointers();

    char exe_dir[512];
    exe_cwd(exe_dir, sizeof(exe_dir));
    strncpy(system_dir, exe_dir[0] ? exe_dir : ".", sizeof(system_dir));

    core_handle = load_library(path);
    if (!core_handle) {
#ifndef _WIN32
        const char *err = dlerror();
        fprintf(stderr, "[libretro] dlopen('%s') failed: %s\n", path, err ? err : "(no error)");
#else
        fprintf(stderr, "[libretro] LoadLibrary('%s') failed: %lu\n", path, (unsigned long)GetLastError());
#endif
        return false;
    }
    printf("Libretro: loaded core from %s\n", path);

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
    if (!native_libretro_game_load_only(path)) return false;
    native_libretro_game_finalize();
    return true;
}

bool native_libretro_game_load_only(const char *path) {
    if (!core_handle) return false;

    char full_path[1024];
    if (path[0] != '/' && path[0] != '.' && !(path[0] != '\0' && path[1] == ':')) {
        char exe_dir[512];
        exe_cwd(exe_dir, sizeof(exe_dir));
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

    if (p_retro_set_video_refresh)      p_retro_set_video_refresh(core_video_refresh);
    if (p_retro_set_audio_sample)       p_retro_set_audio_sample(core_audio_sample);
    if (p_retro_set_audio_sample_batch) p_retro_set_audio_sample_batch(core_audio_sample_batch);
    if (p_retro_set_input_poll)         p_retro_set_input_poll(core_input_poll);
    if (p_retro_set_input_state)        p_retro_set_input_state(engine_input_state_cb);

    if (!core_init_done) {
        if (p_retro_init) {
            printf("Libretro: calling retro_init\n");
            p_retro_init();
        }
        core_init_done = true;
    }

    if (p_retro_set_controller_port_device) p_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

    printf("Libretro: Loading game: %s\n", full_path);
    bool ok = p_retro_load_game ? p_retro_load_game(&info) : false;

    if (info.data) free((void*)info.data);

    if (ok) {
        memset(&s_pending_av_info, 0, sizeof(s_pending_av_info));
        if (p_retro_get_system_av_info) p_retro_get_system_av_info(&s_pending_av_info);
        s_pending_finalize = true;
    }
    return ok;
}

/* Roda no main thread após uv_thread_join do worker.
 * hw_context_reset toca GL, audio_configure toca o ring buffer; nenhum
 * dos dois é seguro fora do main. */
void native_libretro_game_finalize(void) {
    if (!s_pending_finalize) return;
    s_pending_finalize = false;

    if (media_bind() && media.audio_configure) media.audio_configure((unsigned)s_pending_av_info.timing.sample_rate, 2);
    if (libretro_hw_is_active()) {
        int fw = (s_pending_av_info.geometry.max_width  > 0)
            ? (int)s_pending_av_info.geometry.max_width
            : (int)s_pending_av_info.geometry.base_width;
        int fh = (s_pending_av_info.geometry.max_height > 0)
            ? (int)s_pending_av_info.geometry.max_height
            : (int)s_pending_av_info.geometry.base_height;
        libretro_hw_context_reset(fw, fh);
    }
    if (media_bind()) media.claim();
    core_initialized = true;
    api->registry("set", "core:state", (void *)(uintptr_t)GECND_FSM_RUNNING_PERFORMANCE, NULL);
}

/* Grava o buffer em /tmp/<name> para cores com need_fullpath.
 * Mantém a extensão original — cores detectam o console por ela. */
static const char *libretro_spill_rom_to_tmp(const uint8_t *data, size_t size,
                                             const char *name,
                                             const struct retro_system_info *sys_info) {
    char fallback[32] = "rom";
    if ((!name || !name[0]) && sys_info->valid_extensions) {
        char ext[16];
        snprintf(ext, sizeof(ext), "%s", sys_info->valid_extensions);
        char *sep = strchr(ext, '|');
        if (sep) *sep = '\0';
        snprintf(fallback, sizeof(fallback), "rom.%s", ext);
    }
    snprintf(s_tmp_rom_path, sizeof(s_tmp_rom_path), "/tmp/%s",
             (name && name[0]) ? name : fallback);

    FILE *f = fopen(s_tmp_rom_path, "wb");
    if (!f) {
        native_libretro_set_error("cannot create tmp rom: %s", s_tmp_rom_path);
        s_tmp_rom_path[0] = '\0';
        return NULL;
    }
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    if (written != size) {
        native_libretro_set_error("short write on tmp rom: %s", s_tmp_rom_path);
        unlink(s_tmp_rom_path);
        s_tmp_rom_path[0] = '\0';
        return NULL;
    }
    return s_tmp_rom_path;
}

bool native_libretro_game_from_buffer(const uint8_t *data, size_t size, const char *name) {
    if (!core_handle) return false;

    struct retro_system_info sys_info = {0};
    if (p_retro_get_system_info) p_retro_get_system_info(&sys_info);

    struct retro_game_info info = {0};
    info.path = NULL;
    info.data = data;
    info.size = size;
    info.meta = NULL;

    if (sys_info.need_fullpath) {
        info.path = libretro_spill_rom_to_tmp(data, size, name, &sys_info);
        if (!info.path) return false;
        info.data = NULL;
        info.size = 0;
    }

    if (p_retro_set_video_refresh)      p_retro_set_video_refresh(core_video_refresh);
    if (p_retro_set_audio_sample)       p_retro_set_audio_sample(core_audio_sample);
    if (p_retro_set_audio_sample_batch) p_retro_set_audio_sample_batch(core_audio_sample_batch);
    if (p_retro_set_input_poll)         p_retro_set_input_poll(core_input_poll);
    if (p_retro_set_input_state)        p_retro_set_input_state(engine_input_state_cb);

    if (!core_init_done) {
        if (p_retro_init) p_retro_init();
        core_init_done = true;
    }

    if (p_retro_set_controller_port_device)
        p_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

    bool ok = p_retro_load_game ? p_retro_load_game(&info) : false;
    if (ok) {
        memset(&s_pending_av_info, 0, sizeof(s_pending_av_info));
        if (p_retro_get_system_av_info) p_retro_get_system_av_info(&s_pending_av_info);
        s_pending_finalize = true;
    }
    return ok;
}

void native_libretro_exit(void) {
    libretro_deinit_core();
}

static void libretro_deinit_core(void) {
    api->registry("set", "core:state", (void *)(uintptr_t)GECND_FSM_RUNNING, NULL);
    if (core_initialized) {
        if (p_retro_unload_game) p_retro_unload_game();
    }
    if (s_tmp_rom_path[0]) {
        unlink(s_tmp_rom_path);
        s_tmp_rom_path[0] = '\0';
    }
    libretro_hw_cleanup();
    if (core_init_done) {
        if (p_retro_deinit) p_retro_deinit();
    }
    if (core_handle) close_library(core_handle);
    if (core_initialized && media_bind()) {
        static const uint8_t blank[4] = { 0, 0, 0, 0 };
        if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
            media.push_xrgb8888(blank, 1, 1, 4);
        else
            media.push_rgb565(blank, 1, 1, 2);
    }
    if (media_bind()) media.release();
    core_initialized = core_init_done = false;
    core_handle = NULL;
    reset_pointers();
}

MediaFrame *libretro_get_frame(void) {
    return media_bind() ? media.get_frame() : NULL;
}

void libretro_run_frame(void) {
    if (!core_initialized || !p_retro_run) return;
    p_retro_run();
    if (libretro_hw_is_active()) libretro_hw_restore_context();
}

bool libretro_is_running(void) {
    return core_initialized;
}

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
