#include <string.h>
#include <stdlib.h>
#include <lauxlib.h>
#include <lua.h>

#define GLY_HOOK_IMPL
#include "gehook.h"
#include "gecnd.h"
#include "gemetrics.h"



void gamely_daemon_webloop_start(void *loop);

typedef struct {
    FILE *fp;
    char buf[256];
} gecnd_buffer_t;

extern int gecnd_signal;

static const char *reader(lua_State *L, void *ud, size_t *size) {
    (void)L;
    gecnd_buffer_t *data = (gecnd_buffer_t *)ud;
    size_t n = fread(data->buf, 1, sizeof(data->buf), data->fp);
    *size = n;
    return n ? data->buf : NULL;
}

/**
 * @todo rewrite this bullshit code!
 */
static const char *open_script(gecnd_t *gly, const char *lua_code, const char *lua_name, int lua_ret)
{
    gecnd_buffer_t data;
    char path[512];

    if (!lua_code) {
        size_t len = gecnd_utils_get_exe_cwd(path, sizeof(path));
        snprintf(path + len, sizeof(path) - len, "/%s.lua", lua_name);
        printf("[%s]\n", path);
        lua_code = path;
    }

    data.fp = fopen(lua_code, "rb");
    if (!data.fp) {
        snprintf(path, sizeof(path), "file not found: %s.lua", lua_name);
        return strdup(path);
    }

#if LUA_VERSION_NUM >= 502
    if (lua_load(gly->L, reader, &data, lua_name, "t"))
#else
    if (lua_load(gly->L, reader, &data, lua_name))
#endif
    {
        fclose(data.fp);
        return luaL_checkstring(gly->L, -1);
    }
    fclose(data.fp);

    if (lua_pcall(gly->L, 0, lua_ret, 0))
        return luaL_checkstring(gly->L, -1);

    return NULL;
}

static void on_input_key(const char *name, bool pressed, int port, void *usr)
{
    gecnd_dispatch_key_event((gecnd_t *)usr, name, pressed, port);
}

static void callback_init(gecnd_t *gly) {
    do {
        gly_hook_display_init(gly->width, gly->height);

        if (gly->window_width == 0 || gly->window_height == 0) {
            gly->window_width = gly->width;
            gly->window_height = gly->height;
        }

        if (gecnd_is_root(gly)) {
            if (gencd_filter_is_zero_corners())  gecnd_filter_reset_corners();
            if (gencd_filter_is_zero_video_pos()) gecnd_filter_reset_video_pos();
            gamely_daemon_webloop_start(gly->loop);
            gamely_daemon_webclient_start(gly->loop);
            gamely_daemon_webserver_start(gly->loop, gly->port);
            gamely_daemon_fs_start(gly->loop);
            gamely_daemon_db_start();
            gamely_daemon_img_start(gly->loop);
            gly_hook_daemon_img_backend_register();
            gamely_daemon_io_resolver_start();
            gamely_daemon_webclient_img_register();
            gamely_daemon_input_open();
            gamely_daemon_input_subscribe(on_input_key, gly);
        }

        if (gly->loop)
            gly_hook_display_fps(0);
        else
            gly_hook_display_fps(gly->target_fps);

        gly->error_string = gecnd_plugins_open_lua(gly->L);
        if (gly->error_string) break;

        gly->error_string = open_script(gly, gly->lua_engine_code, "main", 0);
        if (gly->error_string) break;

        lua_getglobal(gly->L, "native_callback_init");
        if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
            gly->error_string = "missing: native_callback_init";
            break;
        }

        lua_pushnumber(gly->L, gly->width);
        lua_pushnumber(gly->L, gly->height);

        gly->error_string = open_script(gly, gly->lua_game_code, "game", 1);
        if (gly->error_string) break;

        if (lua_pcall(gly->L, 3, 0, 0)) {
            gly->error_string = luaL_checkstring(gly->L, -1);
            break;
        }

        lua_getglobal(gly->L, "native_callback_draw");
        if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
            gly->error_string = "native_callback_draw";
            break;
        }
        gly->ref_native_callback_draw = luaL_ref(gly->L, LUA_REGISTRYINDEX);

        lua_getglobal(gly->L, "native_callback_loop");
        if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
            gly->error_string = "native_callback_loop";
            break;
        }
        gly->ref_native_callback_loop = luaL_ref(gly->L, LUA_REGISTRYINDEX);

        lua_getglobal(gly->L, "native_callback_keyboard");
        if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
            gly->error_string = "native_callback_keyboard";
            break;
        }
        gly->ref_native_callback_keyboard = luaL_ref(gly->L, LUA_REGISTRYINDEX);

        /* fire initial unpressed state for all mapped keys */
        if (gecnd_is_root(gly))
            gamely_daemon_input_init_keys(on_input_key, gly);
    }
    while (0);
}

void gecnd_dispatch_key_event(gecnd_t *gly, const char *name, bool pressed, int port)
{
    if (!gly || !name || port != 0) return;

    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
    lua_pushstring(gly->L, name);
    lua_pushboolean(gly->L, pressed);
    if (lua_pcall(gly->L, 2, 0, 0))
        gly->error_string = luaL_checkstring(gly->L, -1);
}

static void callback_keyboard(gecnd_t *gly) {
    uint8_t index = 0;
    do {
        char *key    = NULL;
        bool pressed = false;
        gly_hook_input_keyboard(index, &key, &pressed);
        if (!key && !pressed) break;
        index++;
        if (key) {
            gecnd_dispatch_key_event(gly, key, pressed, 0);
            if (gly->error_string) break;
        }
    }
    while (index < 100);
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

bool gecnd_update(gecnd_t *gly)
{
    bool should_close = false;

    do {
        if (!(gly && !gly->error_string)) break;

        if (!(GECND_INTERNAL_RUNNING & gly->internal))
            callback_init(gly);
        if (gly->error_string) break;

        gly->internal |= GECND_INTERNAL_RUNNING;
        gecnd_metrics_finish_wait();

        gamely_daemon_media_playback_tick();

        gecnd_metrics_start_input();
        if (gecnd_is_root(gly)) {
            gamely_daemon_input_tick();
            gamely_daemon_fs_tick();
        }
        callback_keyboard(gly);
        gecnd_metrics_finish_input();
        if (gly->error_string) break;

        gecnd_metrics_start_loop();
        callback_loop(gly);
        gecnd_metrics_finish_loop();
        if (gly->error_string) break;

        if (gly->internal & GECND_INTERNAL_BROWSER) {
            gecnd_metrics_update();
            gecnd_metrics_start_wait();
            break;
        }

        if (gly->frameskip_count++ >= gly->frameskip) {
            gly->frameskip_count = 0;
            native_draw_start();
            gecnd_metrics_start_draw();
            gly->want_blit = true;
            callback_draw(gly);
            if (gly->want_blit)
                gly->error_string = "[error] engine want blit!\n";
            gecnd_metrics_finish_draw();
            gecnd_metrics_start_post();
            native_draw_flush();
            gecnd_metrics_finish_post();
            gecnd_metrics_render(gly);
            gecnd_metrics_start_tint();
            native_draw_finish();
            gecnd_metrics_finish_tint();
        }

        gecnd_metrics_update();
        gecnd_metrics_start_wait();
        if (gly->error_string) break;
    }
    while (0);

    do {
        should_close = gly->error_string != 0;
        if (should_close) break;
        gly_hook_should_close(&should_close);
        if (should_close) break;
        should_close = gly->internal & GECND_INTERNAL_WANT_EXIT;
        if (should_close) break;
        should_close = gecnd_signal != 0;
    }
    while (0);

    return !should_close;
}
