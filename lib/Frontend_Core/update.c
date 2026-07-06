#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#define GLY_HOOK_IMPL
#include "gehook.h"
#include "gecnd.h"
#include "gdmsp.h"
#include "gdweb.h"
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

static void on_fetch_status(gdweb_id_t id, int status, void *user) {
    (void)id;
    if (status < 200 || status >= 300)
        ((gecnd_lua_source_t *)user)->fetch.error = true;
}

static void on_fetch_data(gdweb_id_t id, const char *data, size_t len, void *user) {
    (void)id;
    gecnd_lua_source_t *src = (gecnd_lua_source_t *)user;
    if (src->fetch.error) return;
    uint8_t *tmp = realloc(src->fetch.buf, src->fetch.len + len);
    if (!tmp) { src->fetch.error = true; return; }
    src->fetch.buf = tmp;
    memcpy(src->fetch.buf + src->fetch.len, data, len);
    src->fetch.len += len;
}

static void on_fetch_done(gdweb_id_t id, void *user) {
    (void)id;
    ((gecnd_lua_source_t *)user)->fetch.done = true;
}

static void on_fetch_error(gdweb_id_t id, const char *msg, void *user) {
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
    if (!gdweb_control_client()->http(src->uri, NULL,
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

static bool init_check_exit(gecnd_t *gly) {
    bool close_requested = false;
    gly_hook_should_close(&close_requested);
    if (close_requested || gecnd_signal != 0) {
        gecnd_signal = 0;
        gly->state = GECND_FSM_EXITING;
    }
    return true;
}

static void on_core_state(const char *key, void *value, void *usr) {
    (void)key;
    gecnd_t *gly = (gecnd_t *)usr;
    if (gly) gecnd_set_state(gly, (gecnd_fsm_t)(uintptr_t)value);
}

static bool state_boot(gecnd_t *gly) {
    gly_hook_display_init(gly->width, gly->height);
    if (gecnd_is_root(gly)) {
        gamely_hypervisor_init(gly);
        gecnd_registry("hook", "core:state", on_core_state, gly);
    }
    gly_hook_display_fps(gly->loop ? 0 : gly->target_fps);
    const char *e = gecnd_plugins_open_lua(gly->L);
    if (e) { gecnd_add_error(gly, "%s", e); return false; }
    gly->state = GECND_FSM_LUALIB_LOADED;
    return init_check_exit(gly);
}

static bool state_lualib_load(gecnd_t *gly) {
    gecnd_registry("get", "boot:*", NULL, gly);
    if (gly->error_len) return false;
    gly->state = GECND_FSM_DAEMONS_UP;
    return init_check_exit(gly);
}

static bool state_daemons_up(gecnd_t *gly) {
#if !defined(GECND_USE_VENDOR_GAME)
    if (gly->game_source.kind == GECND_LUA_SOURCE_NONE &&
        gdmsp_control()->is_active()) {
        gly->state = GECND_FSM_RUNNING_NOGAME;
        return true;
    }
#endif
    if (gly->engine_source.kind == GECND_LUA_SOURCE_HTTP) {
        fetch_start(&gly->engine_source);
        gly->state = GECND_FSM_FETCHING;
        return init_check_exit(gly);
    }
    const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_ENGINE)
    vbuf = engine_bytecode_lua; vlen = engine_bytecode_lua_len;
#endif
    const char *e = load_via_resolver(gly, &gly->engine_source, "main", 0, vbuf, vlen);
    if (e) { gecnd_add_error(gly, "%s", e); return false; }
    gly->state = GECND_FSM_ENGINE_LOADED;
    return init_check_exit(gly);
}

static bool state_fetching(gecnd_t *gly) {
    if (gly->engine_source.kind == GECND_LUA_SOURCE_HTTP) {
        if (!gly->engine_source.fetch.done) return init_check_exit(gly);
        const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_ENGINE)
        vbuf = engine_bytecode_lua; vlen = engine_bytecode_lua_len;
#endif
        const char *e = fetch_load(gly, &gly->engine_source, "main", 0, vbuf, vlen);
        if (e) { gecnd_add_error(gly, "%s", e); return false; }
        gly->engine_source.kind = GECND_LUA_SOURCE_NONE;
        gly->state = GECND_FSM_ENGINE_LOADED;
        return init_check_exit(gly);
    }
    if (!gly->game_source.fetch.done) return init_check_exit(gly);
    const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_GAME)
    vbuf = game_bytecode_lua; vlen = game_bytecode_lua_len;
#endif
    {
        const char *e = fetch_load(gly, &gly->game_source, "game", 1, vbuf, vlen);
        if (e) { gecnd_add_error(gly, "%s", e); return false; }
    }
    gly->state = GECND_FSM_GAME_LOADED;
    return init_check_exit(gly);
}

static bool state_engine_loaded(gecnd_t *gly) {
    lua_getglobal(gly->L, "native_callback_init");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gecnd_add_error(gly, "missing: native_callback_init");
        return false;
    }
    lua_pushnumber(gly->L, gly->width);
    lua_pushnumber(gly->L, gly->height);
    /* stack: [native_callback_init, w, h] — held until state_game_loaded */

    if (gly->game_source.kind == GECND_LUA_SOURCE_HTTP) {
        fetch_start(&gly->game_source);
        gly->state = GECND_FSM_FETCHING;
        return init_check_exit(gly);
    }
    const uint8_t *vbuf = NULL; size_t vlen = 0;
#if defined(GECND_USE_VENDOR_GAME)
    vbuf = game_bytecode_lua; vlen = game_bytecode_lua_len;
#endif
    {
        const char *e = load_via_resolver(gly, &gly->game_source, "game", 1, vbuf, vlen);
        if (e) { gecnd_add_error(gly, "%s", e); return false; }
    }
    gly->state = GECND_FSM_GAME_LOADED;
    return init_check_exit(gly);
}

static bool state_game_loaded(gecnd_t *gly) {
    if (lua_pcall(gly->L, 3, 0, 0)) {
        gecnd_add_error(gly, "%s", lua_tostring(gly->L, -1));
        return false;
    }

    lua_getglobal(gly->L, "native_callback_draw");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gecnd_add_error(gly, "missing: native_callback_draw");
        return false;
    }
    gly->ref_native_callback_draw = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    lua_getglobal(gly->L, "native_callback_loop");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gecnd_add_error(gly, "missing: native_callback_loop");
        return false;
    }
    gly->ref_native_callback_loop = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    lua_getglobal(gly->L, "native_callback_keyboard");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        gecnd_add_error(gly, "missing: native_callback_keyboard");
        return false;
    }
    gly->ref_native_callback_keyboard = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    if (gecnd_is_root(gly))
        gamely_input_add_cb("@init", gecnd_dispatch_key_event, gly);

    gly->state = GECND_FSM_RUNNING;
    return true;
}

/* ── Runtime callbacks ───────────────────────────────────────────── */

void gecnd_dispatch_key_event(const char *name, bool pressed, int port, void *usr) {
    if (!usr || !name || port != 0) return;
    gecnd_t *gly = (gecnd_t *)usr;
    if (gly->ref_native_callback_keyboard <= 0) return;
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
    lua_pushstring(gly->L, name);
    lua_pushboolean(gly->L, pressed);
    lua_pushnumber(gly->L, port);
    if (lua_pcall(gly->L, 3, 0, 0))
        gecnd_add_error(gly, "%s", lua_tostring(gly->L, -1));
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
            gecnd_add_error(gly, "backend not has provider delta time");
            return;
        }
    }

    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_loop);
    lua_pushnumber(gly->L, delta_time);
    if (lua_pcall(gly->L, 1, 0, 0))
        gecnd_add_error(gly, "%s", lua_tostring(gly->L, -1));
}

static void callback_draw(gecnd_t *gly) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_draw);
    if (lua_pcall(gly->L, 0, 0, 0))
        gecnd_add_error(gly, "%s", lua_tostring(gly->L, -1));
}

static bool state_running(gecnd_t *gly) {
    gecnd_metrics_finish_wait();
    gdmsp_control()->tick();
    gdweb_lua_tick();

    gecnd_metrics_start_input();
    if (gecnd_is_root(gly)) gamely_hypervisor_tick();
    gecnd_metrics_finish_input();
    if (gly->error_len) return false;

    gecnd_metrics_start_loop();
    if (gly->state != GECND_FSM_RUNNING_NOGAME)
        callback_loop(gly);
    gecnd_metrics_finish_loop();
    if (gly->error_len) return false;

    if (gly->state != GECND_FSM_RUNNING_BACKGROUND &&
        gly->state != GECND_FSM_RUNNING_STANDBY) {
        if (gly->frameskip_count++ >= gly->frameskip) {
            gly->frameskip_count = 0;
            native_draw_start();
            gecnd_metrics_start_draw();
            if (gly->state != GECND_FSM_RUNNING_NOGAME) {
                gly->want_blit = true;
                callback_draw(gly);
                if (gly->want_blit)
                    gecnd_add_error(gly, "[error] engine want blit!");
            }
            gecnd_metrics_finish_draw();
            gecnd_metrics_render(gly);
            gecnd_metrics_start_post();
            native_draw_flush();
            gecnd_metrics_finish_post();
        }
    }

    gecnd_metrics_update();
    gecnd_metrics_start_wait();
    if (gly->error_len) return false;

    bool close_requested = false;
    gly_hook_should_close(&close_requested);
    if (gly->state == GECND_FSM_RUNNING_NOGAME && !gdmsp_control()->is_active()) {
        gecnd_signal = 0;
        gly->state = GECND_FSM_EXITING;
    } else if (close_requested || gecnd_signal != 0) {
        gecnd_signal = 0;
        gly->state = GECND_FSM_EXITING;
    }
    return true;
}

static bool state_exiting(gecnd_t *gly) {
    if (gecnd_signal != 0) {
        gly->state = GECND_FSM_EXITING_FORCE;
        return false;
    }
    gdmsp_control()->tick();
    return gdmsp_control()->is_active();
}

bool gecnd_update(gecnd_t *gly) {
    if (!gly || gly->error_len || gly->state == GECND_FSM_EXITING_FORCE) return false;
    switch (gly->state) {
    case GECND_FSM_BOOT:
    case GECND_FSM_ARGS_PARSED:    return state_boot(gly);
    case GECND_FSM_DAEMONS_UP:     return state_daemons_up(gly);
    case GECND_FSM_FETCHING:       return state_fetching(gly);
    case GECND_FSM_LUALIB_LOADED:  return state_lualib_load(gly);
    case GECND_FSM_ENGINE_LOADED:  return state_engine_loaded(gly);
    case GECND_FSM_GAME_LOADED:    return state_game_loaded(gly);
    case GECND_FSM_RUNNING:
    case GECND_FSM_RUNNING_PERFORMANCE:
    case GECND_FSM_RUNNING_BACKGROUND:
    case GECND_FSM_RUNNING_STANDBY:
    case GECND_FSM_RUNNING_NOGAME: return state_running(gly);
    case GECND_FSM_EXITING:        return state_exiting(gly);
    default:                       return false;
    }
}
