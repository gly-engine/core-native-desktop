#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <uv.h>

#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"

#include "uri_query.h"

/* Protótipos de open_libretro.c */
bool        native_libretro_load               (const char *core_path);
bool        native_libretro_game               (const char *rom_path);
bool        native_libretro_game_load_only     (const char *rom_path);
bool        native_libretro_game_from_buffer   (const uint8_t *data, size_t size);
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

/* ── async load infrastructure (compartilhada file + http) ────────── */

typedef enum {
    LOAD_KIND_FILE = 0,
    LOAD_KIND_BUFFER,
} load_kind_t;

static uv_thread_t s_load_thread;
static atomic_bool s_load_running = false;
static atomic_bool s_load_done    = false;
static atomic_bool s_load_ok      = false;
static atomic_bool s_http_fetching = false;  /* entre source() e on_done() */

static load_kind_t s_load_kind;
static char        s_load_core_path[1024];
static char        s_load_rom_path[1024];
static uint8_t    *s_load_rom_buf = NULL;
static size_t      s_load_rom_buf_len = 0;

static void load_worker(void *arg) {
    (void)arg;
    bool ok = native_libretro_load(s_load_core_path);
    if (ok) {
        if (s_load_kind == LOAD_KIND_FILE)
            ok = native_libretro_game_load_only(s_load_rom_path);
        else
            ok = native_libretro_game_from_buffer(s_load_rom_buf, s_load_rom_buf_len);
    }
    /* buffer não é mais útil depois que load_game o copia internamente */
    if (s_load_rom_buf) {
        free(s_load_rom_buf);
        s_load_rom_buf     = NULL;
        s_load_rom_buf_len = 0;
    }
    atomic_store(&s_load_ok,   ok);
    atomic_store(&s_load_done, true);
}

/* Chamado no main thread (via TICK) — faz join não-bloqueante e dispara
 * finalize (GL/audio/state) que precisa rodar fora do worker. */
static void poll_load_completion(void) {
    if (!atomic_load(&s_load_running)) return;
    if (!atomic_load(&s_load_done))    return;
    uv_thread_join(&s_load_thread);
    atomic_store(&s_load_running, false);
    if (atomic_load(&s_load_ok)) {
        native_libretro_game_finalize();
    } else {
        fprintf(stderr, "[libretro] background load failed\n");
        native_libretro_exit();
    }
}

static void abort_load_if_running(void) {
    if (atomic_load(&s_load_running)) {
        uv_thread_join(&s_load_thread);
        atomic_store(&s_load_running, false);
    }
    if (s_load_rom_buf) {
        free(s_load_rom_buf);
        s_load_rom_buf     = NULL;
        s_load_rom_buf_len = 0;
    }
}

/* ── helpers de parsing de URL composta ───────────────────────────── */

static void parse_libretro_url(const char *url,
                                char *core_out,  size_t core_sz,
                                char *trans_out, size_t trans_sz,
                                char *path_out,  size_t path_sz,
                                char *query_out, size_t query_sz) {
    if (core_out)  core_out[0]  = '\0';
    if (trans_out) trans_out[0] = '\0';
    if (path_out)  path_out[0]  = '\0';
    if (query_out) query_out[0] = '\0';

    const char *sep = strstr(url, "://");
    if (!sep) {
        if (path_out) strncpy(path_out, url, path_sz - 1);
        return;
    }

    char schema[256];
    size_t slen = (size_t)(sep - url);
    if (slen >= sizeof(schema)) return;
    memcpy(schema, url, slen);
    schema[slen] = '\0';

    char *tok = strtok(schema, "+");
    while (tok) {
        if (strcmp(tok, "libretro") == 0) {
            /* skip */
        } else if ((strcmp(tok, "http") == 0 || strcmp(tok, "https") == 0)) {
            if (trans_out && trans_out[0] == '\0')
                strncpy(trans_out, tok, trans_sz - 1);
        } else {
            if (core_out && core_out[0] == '\0')
                strncpy(core_out, tok, core_sz - 1);
        }
        tok = strtok(NULL, "+");
    }

    const char *rest  = sep + 3;
    const char *qmark = strchr(rest, '?');
    if (qmark) {
        size_t plen = (size_t)(qmark - rest);
        if (path_out) {
            size_t copy = plen < path_sz - 1 ? plen : path_sz - 1;
            memcpy(path_out, rest, copy);
            path_out[copy] = '\0';
        }
        if (query_out) strncpy(query_out, qmark + 1, query_sz - 1);
    } else {
        if (path_out) strncpy(path_out, rest, path_sz - 1);
    }
}

/* ── player: libretro+? (arquivo local via scanner) ──────────────── */

static void libretro_file_source(uint8_t channel, const char *url, void *usr) {
    (void)channel; (void)usr;

    /* tears down any previously loaded core + cancela load em curso */
    abort_load_if_running();
    native_libretro_exit();

    char core_name[64], path[512], query[256];
    parse_libretro_url(url, core_name, sizeof(core_name),
                       NULL, 0,
                       path, sizeof(path),
                       query, sizeof(query));

    uri_query_parse(query[0] ? query : NULL);

    /* scanner_resolve_core/rom retornam ponteiro pra buffer estático único —
     * copia antes da próxima chamada pra não sobrescrever. */
    const char *core_path = scanner_resolve_core(core_name);
    if (!core_path) {
        fprintf(stderr, "[libretro] core not found: %s\n", core_name);
        return;
    }
    strncpy(s_load_core_path, core_path, sizeof(s_load_core_path) - 1);
    s_load_core_path[sizeof(s_load_core_path) - 1] = '\0';

    const char *rom_path = scanner_resolve_rom(path);
    if (!rom_path) {
        fprintf(stderr, "[libretro] rom not found: %s\n", path);
        return;
    }
    strncpy(s_load_rom_path, rom_path, sizeof(s_load_rom_path) - 1);
    s_load_rom_path[sizeof(s_load_rom_path) - 1] = '\0';

    s_load_kind = LOAD_KIND_FILE;
    atomic_store(&s_load_done, false);
    atomic_store(&s_load_ok,   false);
    atomic_store(&s_load_running, true);
    if (uv_thread_create(&s_load_thread, load_worker, NULL) != 0) {
        atomic_store(&s_load_running, false);
        fprintf(stderr, "[libretro] failed to spawn load thread\n");
    }
}

static void libretro_file_command(uint8_t channel, gdmsp_cmd_t cmd, void *usr) {
    (void)channel; (void)usr;
    switch (cmd) {
        case GDMSP_CMD_STOP:
            abort_load_if_running();
            native_libretro_exit();
            break;
        case GDMSP_CMD_TICK: {
            gecnd_t *gly = gecnd_get_root();
            if (gly && (gly->internal & GECND_INTERNAL_HW_GL_READY))
                libretro_hw_gl_ready();
            poll_load_completion();
            libretro_run_frame();
            break;
        }
        case GDMSP_CMD_PLAY:
        case GDMSP_CMD_PAUSE:
            /* sem suporte a pause real por enquanto */
            break;
    }
}

static gdmsp_fsm_t libretro_file_state(uint8_t channel, void *usr) {
    (void)channel; (void)usr;
    if (atomic_load(&s_http_fetching) || atomic_load(&s_load_running))
        return GDMSP_FSM_LOADING;
    return libretro_is_running() ? GDMSP_FSM_PLAYING : GDMSP_FSM_IDLE;
}

static gamely_media_player_t libretro_file_player = {
    .source  = libretro_file_source,
    .command = libretro_file_command,
    .state   = libretro_file_state,
};

/* ── player: libretro+http+? / libretro+https+? (fetch + buffer) ─── */

typedef struct {
    uint8_t  channel;
    char     core_name[64];
    uint8_t *buf;
    size_t   buf_len;
    size_t   buf_cap;
} libretro_http_ctx_t;

static void libretro_http_on_status(gly_req_id_t id, int status, void *user) {
    (void)id; (void)status;
    libretro_http_ctx_t *ctx = user;
    ctx->buf_len = 0;
}

static void libretro_http_on_data(gly_req_id_t id, const char *data, size_t len, void *user) {
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

static void libretro_http_on_done(gly_req_id_t id, void *user) {
    (void)id;
    libretro_http_ctx_t *ctx = user;

    atomic_store(&s_http_fetching, false);

    const char *core_path = scanner_resolve_core(ctx->core_name);
    if (!core_path) {
        fprintf(stderr, "[libretro+http] core not found: %s\n", ctx->core_name);
        free(ctx->buf);
        free(ctx);
        return;
    }

    /* transfere ownership do buffer pro worker thread */
    strncpy(s_load_core_path, core_path, sizeof(s_load_core_path) - 1);
    s_load_core_path[sizeof(s_load_core_path) - 1] = '\0';
    s_load_rom_buf     = ctx->buf;
    s_load_rom_buf_len = ctx->buf_len;
    ctx->buf = NULL;

    s_load_kind = LOAD_KIND_BUFFER;
    atomic_store(&s_load_done, false);
    atomic_store(&s_load_ok,   false);
    atomic_store(&s_load_running, true);
    if (uv_thread_create(&s_load_thread, load_worker, NULL) != 0) {
        atomic_store(&s_load_running, false);
        free(s_load_rom_buf);
        s_load_rom_buf = NULL;
        fprintf(stderr, "[libretro+http] failed to spawn load thread\n");
    }
    free(ctx);
}

static void libretro_http_on_error(gly_req_id_t id, const char *msg, void *user) {
    (void)id;
    libretro_http_ctx_t *ctx = user;
    fprintf(stderr, "[libretro+http] fetch error: %s\n", msg ? msg : "");
    atomic_store(&s_http_fetching, false);
    free(ctx->buf);
    free(ctx);
}

static void libretro_http_source(uint8_t channel, const char *url, void *usr) {
    (void)usr;

    abort_load_if_running();
    native_libretro_exit();

    char core_name[64], transport[8], path[512], query[256];
    parse_libretro_url(url,
                       core_name,  sizeof(core_name),
                       transport,  sizeof(transport),
                       path,       sizeof(path),
                       query,      sizeof(query));

    if (!core_name[0] || !transport[0] || !path[0]) {
        fprintf(stderr, "[libretro+http] malformed url: %s\n", url);
        return;
    }

    char fetch_url[768];
    if (query[0])
        snprintf(fetch_url, sizeof(fetch_url), "%s://%s?%s", transport, path, query);
    else
        snprintf(fetch_url, sizeof(fetch_url), "%s://%s", transport, path);

    libretro_http_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return;
    ctx->channel = channel;
    strncpy(ctx->core_name, core_name, sizeof(ctx->core_name) - 1);

    atomic_store(&s_http_fetching, true);

    gly_http_req_t req = {0};
    gamely_daemon_webclient_http(fetch_url, &req,
        libretro_http_on_status, libretro_http_on_data,
        libretro_http_on_done,   libretro_http_on_error,
        ctx);
}

static gamely_media_player_t libretro_http_player = {
    .source  = libretro_http_source,
    .command = libretro_file_command,  /* mesmo tick/stop do file */
    .state   = libretro_file_state,
};

/* ── Lua API ──────────────────────────────────────────────────────── */

static int lua_native_libretro_url(lua_State *L) {
    const char *url = lua_tostring(L, 1);
    if (!url) { lua_pushboolean(L, 0); return 1; }
    char media_url[2048];
    snprintf(media_url, sizeof(media_url), "libretro+%s", url);
    gamely_daemon_media_playback_source(0, media_url);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_native_libretro_exit(lua_State *L) {
    (void)L;
    gamely_daemon_media_playback_stop(0);
    return 0;
}

static int lua_native_libretro_error(lua_State *L) {
    lua_pushstring(L, native_libretro_error());
    return 1;
}

static int lua_libretro_is_running(lua_State *L) {
    lua_pushboolean(L, libretro_is_running());
    return 1;
}

int luaopen_libretro_gecnd(lua_State *L) {
    const luaL_Reg api[] = {
        { "native_libretro_url",    lua_native_libretro_url    },
        { "native_libretro_exit",   lua_native_libretro_exit   },
        { "native_libretro_error",  lua_native_libretro_error  },
        { "native_libretro_is_running", lua_libretro_is_running },
        { NULL, NULL }
    };
    for (int i = 0; api[i].name; i++)
        lua_register(L, api[i].name, api[i].func);
    return 0;
}

void coreopen_libretro_gecnd(void) {
    gamely_daemon_media_register_player("libretro+?",        &libretro_file_player, NULL);
    gamely_daemon_media_register_player("libretro+http+?",   &libretro_http_player, NULL);
    gamely_daemon_media_register_player("libretro+https+?",  &libretro_http_player, NULL);
}
