#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

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

/* ── estado compartilhado file + http ────────────────────────────── */

/* finalize deve rodar no main thread (GL/audio); source() deixa esse flag
 * para o primeiro TICK consumir. */
static bool s_needs_finalize = false;

/* http: sinaliza conclusão do fetch para source() que está bloqueado */
static atomic_bool s_http_fetching = false;
static atomic_bool s_http_ok       = false;

static char     s_load_core_path[1024];
static uint8_t *s_load_rom_buf     = NULL;
static size_t   s_load_rom_buf_len = 0;

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

    char core_name[64], path[512], query[256];
    parse_libretro_url(url, core_name, sizeof(core_name),
                       NULL, 0,
                       path, sizeof(path),
                       query, sizeof(query));

    uri_query_parse(query[0] ? query : NULL);

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

    const char *rom_path = scanner_resolve_rom(path);
    if (!rom_path) {
        fprintf(stderr, "[libretro] rom not found: %s\n", path);
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

static gdmsp_fsm_t libretro_command(uint8_t channel, gdmsp_cmd_t cmd, void *usr) {
    (void)channel; (void)usr;
    switch (cmd) {
        case GDMSP_CMD_RESOURCE:
        case GDMSP_CMD_STOP:
            s_needs_finalize = false;
            native_libretro_exit();
            break;
        case GDMSP_CMD_TICK: {
            if (s_needs_finalize) {
                gecnd_t *gly = gecnd_get_root();
                if (gly && (gly->internal & GECND_INTERNAL_HW_GL_READY))
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

static gamely_media_player_t libretro_file_player = {
    .source  = libretro_file_source,
    .command = libretro_command,
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

static void libretro_http_on_error(gly_req_id_t id, const char *msg, void *user) {
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

    char core_name[64], transport[8], path[512], query[256];
    parse_libretro_url(url,
                       core_name,  sizeof(core_name),
                       transport,  sizeof(transport),
                       path,       sizeof(path),
                       query,      sizeof(query));

    if (!core_name[0] || !transport[0] || !path[0]) {
        fprintf(stderr, "[libretro+http] malformed url: %s\n", url);
        return GDMSP_FSM_ERROR;
    }

    char fetch_url[768];
    if (query[0])
        snprintf(fetch_url, sizeof(fetch_url), "%s://%s?%s", transport, path, query);
    else
        snprintf(fetch_url, sizeof(fetch_url), "%s://%s", transport, path);

    libretro_http_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return GDMSP_FSM_ERROR;
    ctx->channel = channel;
    strncpy(ctx->core_name, core_name, sizeof(ctx->core_name) - 1);

    atomic_store(&s_http_ok,       false);
    atomic_store(&s_http_fetching, true);

    gly_http_req_t req = {0};
    gamely_daemon_webclient_http(fetch_url, &req,
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
    bool ok = native_libretro_game_from_buffer(s_load_rom_buf, s_load_rom_buf_len);
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

static gamely_media_player_t libretro_http_player = {
    .source  = libretro_http_source,
    .command = libretro_command,
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
        { "native_libretro_url",        lua_native_libretro_url    },
        { "native_libretro_exit",       lua_native_libretro_exit   },
        { "native_libretro_error",      lua_native_libretro_error  },
        { "native_libretro_is_running", lua_libretro_is_running    },
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
