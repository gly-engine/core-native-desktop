#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

#include "gecnd.h"
#include "gdmsp.h"
#include "gdweb.h"
#include "main.h"

/* Protótipos de open_libretro.c */
bool        native_libretro_load               (const char *core_path);
bool        native_libretro_game               (const char *rom_path);
bool        native_libretro_game_load_only     (const char *rom_path);
bool        native_libretro_game_from_buffer   (const uint8_t *data, size_t size, const char *name);
void        native_libretro_game_finalize      (void);
bool        native_libretro_url                (const char *url);
void        native_libretro_exit               (void);
void        libretro_run_frame                 (void);
bool        libretro_is_running                (void);
const char *native_libretro_error              (void);

/* Protótipos de hw_render.c */
void libretro_hw_gl_ready(void);

/* Protótipos de scanner.c */
const char *scanner_resolve_core(const char *name);
const char *scanner_resolve_rom (const char *name);

gecnd_api_t *api = NULL;

static struct {
    typeof(gdweb_control_client) *client;
    typeof(gdmsp_control)        *control;
} host;

static bool host_bind(void) {
    if (host.control) return true;
    api->registry("get", "function:gdweb_control_client", (void *)&host.client,  NULL);
    api->registry("get", "function:gdmsp_control",        (void *)&host.control, NULL);
    return host.control != NULL;
}

/* ── estado compartilhado file + http ────────────────────────────── */

/* finalize deve rodar no main thread (GL/audio); source() deixa esse flag
 * para o primeiro TICK consumir. */
static bool s_needs_finalize = false;

/* http: sinaliza conclusão do fetch para source() que está bloqueado */
static atomic_bool s_http_fetching = false;
static atomic_bool s_http_ok       = false;

static char     s_load_core_path[1024];
static char     s_load_rom_name[256];
static uint8_t *s_load_rom_buf     = NULL;
static size_t   s_load_rom_buf_len = 0;

typedef struct {
    const char *core;      size_t core_len;
    const char *location;  size_t location_len;
    const char *remote;
} libretro_url_t;

static libretro_url_t libretro_url_split(const char *url) {
    libretro_url_t r   = {0};
    const char    *loc = NULL;
    size_t         end = 0;
    gecnd_lang_t   ctx = {{ "url", url }};
    while (api->lang(&ctx)) {
        switch (ctx.url.kind) {
        case GECND_URL_KIND_SCHEME:
            if (ctx.url.idx == 1) {
                r.core     = ctx.url.ptr;
                r.core_len = ctx.url.len;
            } else if (ctx.url.idx == 2) {
                r.remote = ctx.url.ptr;
            }
            break;
        case GECND_URL_KIND_HOST:
        case GECND_URL_KIND_PATH:
            if (loc == NULL) {
                loc = ctx.url.ptr;
            }
            end = (size_t)(ctx.url.ptr + ctx.url.len - loc);
            break;
        default:
            break;
        }
    }
    r.location     = loc;
    r.location_len = loc ? end : 0;
    return r;
}

/* ── estado interno do driver ────────────────────────────────────── */

static gdmsp_fsm_t libretro_get_state(void) {
    if (s_needs_finalize)       return GDMSP_FSM_LOADING;
    if (libretro_is_running())  return GDMSP_FSM_PLAYING;
    return GDMSP_FSM_IDLE;
}

/* ── player: libretro+? (arquivo local via scanner) ──────────────── */

static gdmsp_fsm_t libretro_file_source(uint8_t channel, const char *url, void *usr) {
    (void)channel; (void)usr;

    native_libretro_exit();

    libretro_url_t u = libretro_url_split(url);
    url_env_set(url);

    char core_name[64], rom_name[512];
    snprintf(core_name, sizeof(core_name), "%.*s", (int)u.core_len,     u.core     ? u.core     : "");
    snprintf(rom_name,  sizeof(rom_name),  "%.*s", (int)u.location_len, u.location ? u.location : "");

    const char *core_path = scanner_resolve_core(core_name);
    if (!core_path) {
        fprintf(stderr, "[libretro] core not found: %s\n", core_name);
        return GDMSP_FSM_ERROR;
    }
    /* scanner_resolve_* retorna ponteiro para buffer estático — copia antes
     * da próxima chamada para não sobrescrever. */
    char core_buf[1024];
    strncpy(core_buf, core_path, sizeof(core_buf) - 1);
    core_buf[sizeof(core_buf) - 1] = '\0';

    const char *rom_path = scanner_resolve_rom(rom_name);
    if (!rom_path) {
        fprintf(stderr, "[libretro] rom not found: %s\n", rom_name);
        return GDMSP_FSM_ERROR;
    }

    if (!native_libretro_load(core_buf)) {
        fprintf(stderr, "[libretro] failed to load core: %s\n", core_buf);
        return GDMSP_FSM_ERROR;
    }
    if (!native_libretro_game_load_only(rom_path)) {
        fprintf(stderr, "[libretro] failed to load game: %s\n", rom_path);
        native_libretro_exit();
        return GDMSP_FSM_ERROR;
    }
    /* finalize (GL/audio) deve rodar no main thread — primeiro TICK vai consumir */
    s_needs_finalize = true;
    return GDMSP_FSM_LOADING;
}

static gdmsp_fsm_t libretro_set(uint8_t channel, gdmsp_cmd_t cmd, gdmsp_value_t value, void *usr) {
    (void)channel; (void)usr; (void)value;
    switch (cmd) {
        case GDMSP_CMD_RESOURCE:
        case GDMSP_CMD_STOP:
            s_needs_finalize = false;
            native_libretro_exit();
            break;
        case GDMSP_CMD_TICK: {
            if (s_needs_finalize) {
                libretro_hw_gl_ready();
                native_libretro_game_finalize();
                s_needs_finalize = false;
            }
            libretro_run_frame();
            break;
        }
        case GDMSP_CMD_PLAY:
        case GDMSP_CMD_PAUSE:
        default:
            break;
    }
    return libretro_get_state();
}

static gdmsp_value_t libretro_get(uint8_t channel, gdmsp_cmd_t cmd, void *usr) {
    (void)channel; (void)cmd; (void)usr;
    gdmsp_value_t value = {-1};
    return value;
}

static gdmsp_player_t libretro_file_player = {
    .src = libretro_file_source,
    .set = libretro_set,
    .get = libretro_get,
};

/* ── player: libretro+http+? / libretro+https+? (fetch + buffer) ─── */

typedef struct {
    uint8_t  channel;
    char     core_name[64];
    uint8_t *buf;
    size_t   buf_len;
    size_t   buf_cap;
} libretro_http_ctx_t;

static void libretro_http_on_status(gdweb_id_t id, int status, void *user) {
    (void)id; (void)status;
    libretro_http_ctx_t *ctx = user;
    ctx->buf_len = 0;
}

static void libretro_http_on_data(gdweb_id_t id, const char *data, size_t len, void *user) {
    (void)id;
    libretro_http_ctx_t *ctx = user;
    if (ctx->buf_len + len > ctx->buf_cap) {
        size_t new_cap = ctx->buf_cap == 0 ? 65536 : ctx->buf_cap * 2;
        while (new_cap < ctx->buf_len + len) new_cap *= 2;
        uint8_t *nb = realloc(ctx->buf, new_cap);
        if (!nb) return;
        ctx->buf     = nb;
        ctx->buf_cap = new_cap;
    }
    memcpy(ctx->buf + ctx->buf_len, data, len);
    ctx->buf_len += len;
}

static void libretro_http_on_done(gdweb_id_t id, void *user) {
    (void)id;
    libretro_http_ctx_t *ctx = user;

    const char *core_path = scanner_resolve_core(ctx->core_name);
    if (!core_path) {
        fprintf(stderr, "[libretro+http] core not found: %s\n", ctx->core_name);
        free(ctx->buf);
        free(ctx);
        atomic_store(&s_http_ok, false);
        atomic_store(&s_http_fetching, false);
        return;
    }

    strncpy(s_load_core_path, core_path, sizeof(s_load_core_path) - 1);
    s_load_core_path[sizeof(s_load_core_path) - 1] = '\0';
    s_load_rom_buf     = ctx->buf; ctx->buf = NULL;
    s_load_rom_buf_len = ctx->buf_len;
    free(ctx);

    atomic_store(&s_http_ok, true);
    atomic_store(&s_http_fetching, false); /* desbloqueia source() */
}

static void libretro_http_on_error(gdweb_id_t id, const char *msg, void *user) {
    (void)id;
    libretro_http_ctx_t *ctx = user;
    fprintf(stderr, "[libretro+http] fetch error: %s\n", msg ? msg : "");
    free(ctx->buf);
    free(ctx);
    atomic_store(&s_http_ok, false);
    atomic_store(&s_http_fetching, false);
}

static gdmsp_fsm_t libretro_http_source(uint8_t channel, const char *url, void *usr) {
    (void)usr;

    native_libretro_exit();

    libretro_url_t u = libretro_url_split(url);
    url_env_set(url);

    if (u.core == NULL || u.remote == NULL || u.location == NULL) {
        fprintf(stderr, "[libretro+http] malformed url: %s\n", url);
        return GDMSP_FSM_ERROR;
    }

    /* basename da URL — vira o nome do arquivo temporário quando o core
     * exige need_fullpath (extensão importa: cores detectam o console por ela) */
    {
        const char *base = u.location;
        for (size_t i = 0; i < u.location_len; i++)
            if (u.location[i] == '/') base = u.location + i + 1;
        size_t base_len = (size_t)(u.location + u.location_len - base);
        snprintf(s_load_rom_name, sizeof(s_load_rom_name), "%.*s", (int)base_len, base);
    }

    libretro_http_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return GDMSP_FSM_ERROR;
    ctx->channel = channel;
    snprintf(ctx->core_name, sizeof(ctx->core_name), "%.*s", (int)u.core_len, u.core);

    atomic_store(&s_http_ok,       false);
    atomic_store(&s_http_fetching, true);

    gdweb_http_req_t req = {0};
    if (!host_bind()) {
        atomic_store(&s_http_fetching, false);
        free(ctx);
        return GDMSP_FSM_ERROR;
    }
    host.client()->http(u.remote, &req,
        libretro_http_on_status, libretro_http_on_data,
        libretro_http_on_done,   libretro_http_on_error,
        ctx);

    /* bloqueia até o main thread disparar on_done/on_error via event loop */
    while (atomic_load(&s_http_fetching))
        usleep(10000);

    if (!atomic_load(&s_http_ok)) return GDMSP_FSM_ERROR;

    if (!native_libretro_load(s_load_core_path)) {
        fprintf(stderr, "[libretro+http] failed to load core: %s\n", s_load_core_path);
        free(s_load_rom_buf); s_load_rom_buf = NULL; s_load_rom_buf_len = 0;
        return GDMSP_FSM_ERROR;
    }
    bool ok = native_libretro_game_from_buffer(s_load_rom_buf, s_load_rom_buf_len, s_load_rom_name);
    /* buffer não é mais útil depois que load_game o copia internamente */
    free(s_load_rom_buf); s_load_rom_buf = NULL; s_load_rom_buf_len = 0;
    if (!ok) {
        fprintf(stderr, "[libretro+http] failed to load game from buffer\n");
        native_libretro_exit();
        return GDMSP_FSM_ERROR;
    }
    s_needs_finalize = true;
    return GDMSP_FSM_LOADING;
}

static gdmsp_player_t libretro_http_player = {
    .src = libretro_http_source,
    .set = libretro_set,
    .get = libretro_get,
};

static char* lua_native_libretro_url(char* url) {
    char media_url[2048];
    snprintf(media_url, sizeof(media_url), "libretro+%s", url);
    if (host_bind()) host.control()->source(0, media_url);
    return NULL;
}

static char* lua_native_libretro_exit(void) {
    if (host_bind()) host.control()->set(0, GDMSP_CMD_STOP, NULL);
    return NULL;
}

static char* lua_native_libretro_error(const char **const ret) {
    *ret = native_libretro_error();
    return NULL;
}

static char* lua_native_libretro_is_running(bool *const ret) {
    *ret = libretro_is_running();
    return NULL;
}

void coreopen_libretro_gecnd(gecnd_plugin_t *const plugin) {
    api = plugin->require("v1");
    api->registry("set", "lua_global_ffi:native_libretro_url+$s+$0", lua_native_libretro_url, NULL);
    api->registry("set", "lua_global_ffi:native_libretro_exit+$0+$0", lua_native_libretro_exit, NULL);
    api->registry("set", "lua_global_ffi:native_libretro_error+$0+$s", lua_native_libretro_error, NULL);
    api->registry("set", "lua_global_ffi:native_libretro_is_running+$0+$b", lua_native_libretro_is_running, NULL);
    api->registry("set", "media_player:libretro+$l$0", &libretro_file_player, NULL);
    api->registry("set", "media_player:libretro+$l+http$0", &libretro_http_player, NULL);
    api->registry("set", "media_player:libretro+$l+https$0", &libretro_http_player, NULL);
}
