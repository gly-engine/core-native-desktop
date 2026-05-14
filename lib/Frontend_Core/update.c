#include <string.h>
#include <stdlib.h>
#include <lauxlib.h>
#include <lua.h>

#define GLY_HOOK_IMPL
#include "gehook.h"
#include "gecnd.h"
#include "gemetrics.h"

#if defined(GECND_USE_VENDOR_ENGINE)
#include "engine_bytecode_lua.h"
#endif

#if defined(GECND_USE_VENDOR_GAME)
#include "game_bytecode_lua.h"
#endif

typedef struct {
    FILE *fp;
    char  buf[256];
} gecnd_buffer_t;

extern int gecnd_signal;

/* ── Lua file reader ─────────────────────────────────────────────── */

static const char *reader(lua_State *L, void *ud, size_t *size) {
    (void)L;
    gecnd_buffer_t *data = (gecnd_buffer_t *)ud;
    size_t n = fread(data->buf, 1, sizeof(data->buf), data->fp);
    *size = n;
    return n ? data->buf : NULL;
}

typedef enum { LOAD_OK, LOAD_NOT_FOUND, LOAD_PARSE_ERROR } load_status_t;

static load_status_t load_file_path(gecnd_t *gly, const char *path,
                                     const char *lua_name, int lua_ret,
                                     const char **err_out) {
    gecnd_buffer_t data;
    data.fp = fopen(path, "rb");
    if (!data.fp) return LOAD_NOT_FOUND;

#if LUA_VERSION_NUM >= 502
    if (lua_load(gly->L, reader, &data, lua_name, "t"))
#else
    if (lua_load(gly->L, reader, &data, lua_name))
#endif
    {
        fclose(data.fp);
        *err_out = luaL_checkstring(gly->L, -1);
        return LOAD_PARSE_ERROR;
    }
    fclose(data.fp);

    if (lua_pcall(gly->L, 0, lua_ret, 0)) {
        *err_out = luaL_checkstring(gly->L, -1);
        return LOAD_PARSE_ERROR;
    }
    return LOAD_OK;
}

/* sync resolver: FILE → vendor (if provided) → {exe_cwd}/{lua_name}.lua */
static const char *load_via_resolver(gecnd_t *gly, const gecnd_lua_source_t *src,
                                      const char *lua_name, int lua_ret,
                                      const uint8_t *vendor_buf, size_t vendor_len) {
    const char *err = NULL;

    if (src && src->kind == GECND_LUA_SOURCE_FILE && src->uri) {
        load_status_t s = load_file_path(gly, src->uri, lua_name, lua_ret, &err);
        if (s == LOAD_OK)          return NULL;
        if (s == LOAD_PARSE_ERROR) return err;
        /* NOT_FOUND → fall through */
    }

    if (vendor_buf && vendor_len) {
        if (luaL_loadbuffer(gly->L, (const char *)vendor_buf, vendor_len, lua_name))
            return luaL_checkstring(gly->L, -1);
        if (lua_pcall(gly->L, 0, lua_ret, 0))
            return luaL_checkstring(gly->L, -1);
        return NULL;
    }

    char path[512];
    size_t len = gecnd_utils_get_exe_cwd(path, sizeof(path));
    snprintf(path + len, sizeof(path) - len, "/%s.lua", lua_name);
    load_status_t s = load_file_path(gly, path, lua_name, lua_ret, &err);
    if (s == LOAD_OK)          return NULL;
    if (s == LOAD_PARSE_ERROR) return err;

    static char nf[128];
    snprintf(nf, sizeof(nf), "file not found: %s.lua", lua_name);
    return nf;
}

/* ── HTTP fetch callbacks (user = gecnd_lua_source_t *src) ──────── */

static void on_fetch_status(gly_req_id_t id, int status, void *user) {
    (void)id;
    if (status < 200 || status >= 300)
        ((gecnd_lua_source_t *)user)->fetch.error = true;
}

static void on_fetch_data(gly_req_id_t id, const char *data, size_t len, void *user) {
    (void)id;
    gecnd_lua_source_t *src = (gecnd_lua_source_t *)user;
    if (src->fetch.error) return;
    uint8_t *tmp = realloc(src->fetch.buf, src->fetch.len + len);
    if (!tmp) { src->fetch.error = true; return; }
    src->fetch.buf = tmp;
    memcpy(src->fetch.buf + src->fetch.len, data, len);
    src->fetch.len += len;
}

static void on_fetch_done(gly_req_id_t id, void *user) {
    (void)id;
    ((gecnd_lua_source_t *)user)->fetch.done = true;
}

static void on_fetch_error(gly_req_id_t id, const char *msg, void *user) {
    (void)id; (void)msg;
    gecnd_lua_source_t *src = (gecnd_lua_source_t *)user;
    src->fetch.error = true;
    src->fetch.done  = true;
}

static void fetch_start(gecnd_lua_source_t *src) {
    free(src->fetch.buf);
    src->fetch.buf   = NULL;
    src->fetch.len   = 0;
    src->fetch.done  = false;
    src->fetch.error = false;
    if (!gamely_daemon_webclient_http(src->uri, NULL,
            on_fetch_status, on_fetch_data, on_fetch_done, on_fetch_error, src)) {
        src->fetch.error = true;
        src->fetch.done  = true;
    }
}

/* Load HTTP result into Lua; on HTTP failure, falls through to vendor/disk. */
static const char *fetch_load(gecnd_t *gly, gecnd_lua_source_t *src,
                               const char *lua_name, int lua_ret,
                               const uint8_t *vendor_buf, size_t vendor_len) {
    if (src->fetch.error) {
        free(src->fetch.buf);
        src->fetch.buf = NULL;
        src->fetch.len = 0;
        gecnd_lua_source_t none = {0};
        if (!load_via_resolver(gly, &none, lua_name, lua_ret, vendor_buf, vendor_len))
            return NULL;
        static char http_err[256];
        snprintf(http_err, sizeof(http_err), "http fetch failed: %s", src->uri ? src->uri : lua_name);
        return http_err;
    }

    if (luaL_loadbuffer(gly->L, (const char *)src->fetch.buf, src->fetch.len, lua_name)) {
        free(src->fetch.buf); src->fetch.buf = NULL;
        return luaL_checkstring(gly->L, -1);
    }
    free(src->fetch.buf); src->fetch.buf = NULL; src->fetch.len = 0;

    if (lua_pcall(gly->L, 0, lua_ret, 0))
        return luaL_checkstring(gly->L, -1);

    return NULL;
}

/* ── State handlers ──────────────────────────────────────────────── */

static void state_boot(gecnd_t *gly) {
    gly_hook_display_init(gly->width, gly->height);
    if (gecnd_is_root(gly))
        gamely_hypervisor_init(gly);
    gly_hook_display_fps(gly->loop ? 0 : gly->target_fps);
    gly->error_string = gecnd_plugins_open_lua(gly->L);
    if (gly->error_string) return;
    gly->state = GECND_FSM_DAEMONS_UP;
}

static void state_daemons_up(gecnd_t *gly) {
    if (gly->engine_source.kind == GECND_LUA_SOURCE_HTTP) {
        fetch_start(&gly->engine_source);
        gly->state = GECND_FSM_FETCHING;
        return;
    }
    const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_ENGINE)
    vbuf = engine_bytecode_lua; vlen = engine_bytecode_lua_len;
#endif
    gly->error_string = load_via_resolver(gly, &gly->engine_source, "main", 0, vbuf, vlen);
    if (gly->error_string) return;
    gly->state = GECND_FSM_ENGINE_LOADED;
}

static void state_fetching(gecnd_t *gly) {
    if (gly->engine_source.kind == GECND_LUA_SOURCE_HTTP) {
        if (!gly->engine_source.fetch.done) return;
        const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_ENGINE)
        vbuf = engine_bytecode_lua; vlen = engine_bytecode_lua_len;
#endif
        gly->error_string = fetch_load(gly, &gly->engine_source, "main", 0, vbuf, vlen);
        if (gly->error_string) return;
        gly->engine_source.kind = GECND_LUA_SOURCE_NONE;
        gly->state = GECND_FSM_ENGINE_LOADED;
        return;
    }
    if (!gly->game_source.fetch.done) return;
    const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_GAME)
    vbuf = game_bytecode_lua; vlen = game_bytecode_lua_len;
#endif
    gly->error_string = fetch_load(gly, &gly->game_source, "game", 1, vbuf, vlen);
    if (gly->error_string) return;
    gly->state = GECND_FSM_GAME_LOADED;
}

static void state_engine_loaded(gecnd_t *gly) {
    lua_getglobal(gly->L, "native_callback_init");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gly->error_string = "missing: native_callback_init";
        return;
    }
    lua_pushnumber(gly->L, gly->width);
    lua_pushnumber(gly->L, gly->height);
    /* stack: [native_callback_init, w, h] — held until state_game_loaded */

    if (gly->game_source.kind == GECND_LUA_SOURCE_HTTP) {
        fetch_start(&gly->game_source);
        gly->state = GECND_FSM_FETCHING;
        return;
    }
    const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_GAME)
    vbuf = game_bytecode_lua; vlen = game_bytecode_lua_len;
#endif
    gly->error_string = load_via_resolver(gly, &gly->game_source, "game", 1, vbuf, vlen);
    if (gly->error_string) return;
    /* stack: [native_callback_init, w, h, game_module] */
    gly->state = GECND_FSM_GAME_LOADED;
}

static void state_game_loaded(gecnd_t *gly) {
    /* stack: [native_callback_init, w, h, game_module] */
    if (lua_pcall(gly->L, 3, 0, 0)) {
        gly->error_string = luaL_checkstring(gly->L, -1);
        return;
    }

    lua_getglobal(gly->L, "native_callback_draw");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gly->error_string = "missing: native_callback_draw";
        return;
    }
    gly->ref_native_callback_draw = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    lua_getglobal(gly->L, "native_callback_loop");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gly->error_string = "missing: native_callback_loop";
        return;
    }
    gly->ref_native_callback_loop = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    lua_getglobal(gly->L, "native_callback_keyboard");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gly->error_string = "missing: native_callback_keyboard";
        return;
    }
    gly->ref_native_callback_keyboard = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    if (gecnd_is_root(gly))
        gamely_daemon_input_init_keys(gecnd_dispatch_key_event, gly);

    gly->state = GECND_FSM_RUNNING;
}

/* ── Runtime callbacks ───────────────────────────────────────────── */

void gecnd_dispatch_key_event(const char *name, bool pressed, int port, void *usr) {
    if (!usr || !name || port != 0) return;
    gecnd_t *gly = (gecnd_t *)usr;
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
    lua_pushstring(gly->L, name);
    lua_pushboolean(gly->L, pressed);
    lua_pushnumber(gly->L, port);
    if (lua_pcall(gly->L, 3, 0, 0))
        gly->error_string = luaL_checkstring(gly->L, -1);
}

/** @todo delete this */
static void callback_keyboard(gecnd_t *gly) {
    uint8_t index = 0;
    do {
        char *key     = NULL;
        bool  pressed = false;
        gly_hook_input_keyboard(index, &key, &pressed);
        if (!key && !pressed) break;
        index++;
        if (key) {
            gecnd_dispatch_key_event(key, pressed, 0, gly);
            if (gly->error_string) break;
        }
    } while (index < 100);
}

static void callback_loop(gecnd_t *gly) {
    int16_t delta_time = gly->delta_time;

    if (gly->flags & GECND_FLAG_TIMER_PREFER_BACKEND) {
        int16_t new_dt = -1;
        gly_hook_display_dt(&new_dt);
        if (gly->flags & GECND_FLAG_TIMER_BACKEND && new_dt != -1)
            delta_time = new_dt;
        else if (gly->flags & GECND_FLAG_TIMER_INTERNAL)
            delta_time = gecnd_get_sleep(gly);
        else {
            gly->error_string = "backend not has provider delta time";
            return;
        }
    }

    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_loop);
    lua_pushnumber(gly->L, delta_time);
    if (lua_pcall(gly->L, 1, 0, 0))
        gly->error_string = luaL_checkstring(gly->L, -1);
}

static void callback_draw(gecnd_t *gly) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_draw);
    if (lua_pcall(gly->L, 0, 0, 0))
        gly->error_string = luaL_checkstring(gly->L, -1);
}

/* ── Main update ─────────────────────────────────────────────────── */

bool gecnd_update(gecnd_t *gly) {
    bool result = false;
    do {
        if (!gly || gly->state == GECND_FSM_ERROR || gly->state == GECND_FSM_EXITING_FORCE) break;

        if (gly->state == GECND_FSM_EXITING) {
            gamely_daemon_media_playback_tick();
            result = gamely_daemon_media_playback_active();
            break;
        }

        /* Advance initialization states each tick; stops at FETCHING_* or RUNNING. */
        gecnd_fsm_t prev;
        do {
            prev = gly->state;
            switch (gly->state) {
            case GECND_FSM_BOOT:
            case GECND_FSM_ARGS_PARSED:  state_boot(gly);         break;
            case GECND_FSM_DAEMONS_UP:   state_daemons_up(gly);   break;
            case GECND_FSM_FETCHING:     state_fetching(gly);     break;
            case GECND_FSM_ENGINE_LOADED: state_engine_loaded(gly); break;
            case GECND_FSM_GAME_LOADED:  state_game_loaded(gly);  break;
            default: break;
            }
            if (gly->error_string) { gly->state = GECND_FSM_ERROR; break; }
        } while (gly->state != prev && gly->state < GECND_FSM_RUNNING);

        if (gly->state == GECND_FSM_ERROR) break;

        /* Still initializing — check for exit signals even here */
        if (gly->state < GECND_FSM_RUNNING) {
            bool close_requested = false;
            gly_hook_should_close(&close_requested);
            if (close_requested || gecnd_signal != 0)
                gly->state = GECND_FSM_EXITING;
            result = true;
            break;
        }

        /* ── Running ──────────────────────────────────────────────────── */

        gecnd_metrics_finish_wait();
        gamely_daemon_media_playback_tick();

        gecnd_metrics_start_input();
        if (gecnd_is_root(gly)) gamely_hypervisor_tick();
        callback_keyboard(gly);
        gecnd_metrics_finish_input();
        if (gly->error_string) { gly->state = GECND_FSM_ERROR; break; }

        gecnd_metrics_start_loop();
        callback_loop(gly);
        gecnd_metrics_finish_loop();
        if (gly->error_string) { gly->state = GECND_FSM_ERROR; break; }

        if (gly->state != GECND_FSM_RUNNING_BACKGROUND &&
            gly->state != GECND_FSM_RUNNING_STANDBY) {
            if (gly->frameskip_count++ >= gly->frameskip) {
                gly->frameskip_count = 0;
                native_draw_start();
                gecnd_metrics_start_draw();
                gly->want_blit = true;
                callback_draw(gly);
                if (gly->want_blit)
                    gly->error_string = "[error] engine want blit!\n";
                gecnd_metrics_finish_draw();
                gecnd_metrics_render(gly);
                gecnd_metrics_start_post();
                native_draw_flush();
                gecnd_metrics_finish_post();
            }
        }

        gecnd_metrics_update();
        gecnd_metrics_start_wait();
        if (gly->error_string) { gly->state = GECND_FSM_ERROR; break; }

        bool close_requested = false;
        gly_hook_should_close(&close_requested);
        if (close_requested || gecnd_signal != 0)
            gly->state = GECND_FSM_EXITING;

        result = (gly->state == GECND_FSM_EXITING)
                 ? gamely_daemon_media_playback_active()
                 : true;
    } while (0);
    return result;
}
