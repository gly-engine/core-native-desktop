#include <string.h>
#include <stdlib.h>
#include <lauxlib.h>
#include <lua.h>

#define GLY_HOOK_IMPL
#include "gehook.h"
#include "gecnd.h"

extern int gecnd_signal;

#define CHUNK 128
typedef struct {
    FILE *fp;
    char buf[CHUNK];
} ReaderData;

static const char *reader(lua_State *L, void *ud, size_t *size) {
    (void) L;
    ReaderData *R = (ReaderData*)ud;
    size_t n = fread(R->buf, 1, CHUNK, R->fp);
    *size = n;
    return n ? R->buf : NULL;
}

/**
 * @todo make thread safe:
 */
static const char* open_script(gecnd_t *gly, char** lua_code, char* lua_name, int lua_ret) {
    char *default_code = gecnd_utils_get_exe_cwd();

    if (!*lua_code) {
        (void) snprintf(default_code, 1024, "%s/%s.lua", default_code, lua_name);
        *lua_code = default_code;
    }

    static ReaderData R;
    R.fp = fopen(*lua_code, "rb");

    if (!R.fp) {
        (void) snprintf(default_code, 1024, "file not found: %s.lua", lua_name);
        return default_code;
    }

    if (lua_load(gly->L, reader, &R, lua_name, "t")) {
        return luaL_checkstring(gly->L, -1);
    }


    if (lua_pcall(gly->L, 0, lua_ret, 0)) {
        return luaL_checkstring(gly->L, -1);
    }

    return NULL;
}

static void callback_init(gecnd_t *gly) {
    do {
        gly_hook_display_init(gly->width, gly->height);

        if (gly->loop) {
            gly_hook_display_fps(0);
        } else {
            gly_hook_display_fps(gly->target_fps);
        }
        
        gly->error_string = open_script(gly, &gly->lua_engine_code, "engine", 0);
        if (gly->error_string) {
            break;
        }
    
        if (lua_getglobal(gly->L, "native_callback_init") != LUA_TFUNCTION) {
            gly->error_string = "missing: native_callback_init";
            break;
        }

        lua_pushnumber(gly->L, gly->width);
        lua_pushnumber(gly->L, gly->height);

        gly->error_string = open_script(gly, &gly->lua_game_code, "game", 1);
        if (gly->error_string) {
            break;
        }

        if(lua_pcall(gly->L, 3, 0, 0) != LUA_OK) {
            gly->error_string = luaL_checkstring(gly->L, -1);
            break;
        }

        if (lua_getglobal(gly->L, "native_callback_draw") != LUA_TFUNCTION) {
            gly->error_string = "native_callback_draw";
            break;
        }
        gly->ref_native_callback_draw = luaL_ref(gly->L, LUA_REGISTRYINDEX);

        if (lua_getglobal(gly->L, "native_callback_loop") != LUA_TFUNCTION) {
            gly->error_string = "native_callback_loop";
            break;
        }
        gly->ref_native_callback_loop = luaL_ref(gly->L, LUA_REGISTRYINDEX);

        if (lua_getglobal(gly->L, "native_callback_keyboard") != LUA_TFUNCTION) {
            gly->error_string = "native_callback_keyboard";
            break;
        }
        gly->ref_native_callback_keyboard = luaL_ref(gly->L, LUA_REGISTRYINDEX);
    }
    while(0);
}

static void callback_keyboard(gecnd_t *gly) {
    uint8_t index = 0;

    do {
        char* key = NULL;
        bool pressed = false;
        gly_hook_input_keyboard(index, &key, &pressed);

        if (!key && !pressed) break;
        index++;

        if (key) {
            lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
            lua_pushstring(gly->L, key);
            lua_pushboolean(gly->L, pressed);
            if (lua_pcall(gly->L, 2, 0, 0) != LUA_OK) {
                gly->error_string = luaL_checkstring(gly->L, -1);
                break;
            }
        }        
    }
    while(index < 100);
}

static void callback_loop(gecnd_t *gly) {
    int16_t delta_time = gly->delta_time;

    if (gly->flags & GECND_FLAG_TIMER_PREFER_BACKEND) {
        int16_t new_dt = -1;
        gly_hook_display_dt(&new_dt);
        if (gly->flags & GECND_FLAG_TIMER_BACKEND && new_dt != -1) {
            delta_time = new_dt;
        }
        else if (gly->flags & GECND_FLAG_TIMER_INTERNAL) {
            delta_time = gecnd_get_sleep(gly);
        }
        else {
            gly->error_string = "backend not has provider delta time";
            return;
        }
    }

    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_loop);
    lua_pushnumber(gly->L, delta_time);
    if (lua_pcall(gly->L, 1, 0, 0) != LUA_OK) {
        gly->error_string = luaL_checkstring(gly->L, -1);
    }
}

static void callback_draw(gecnd_t *gly) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_draw);
    if (lua_pcall(gly->L, 0, 0, 0) != LUA_OK) {
        gly->error_string = luaL_checkstring(gly->L, -1);
    }
}

bool gecnd_update(gecnd_t * gly)
{
    bool should_close = false;

    do {
        if (!(gly && !gly->error_string)) break;

        if (!(GECND_INTERNAL_RUNNING & gly->internal)) {
            callback_init(gly);
        }
        if (gly->error_string) break;
        gly->internal |= GECND_INTERNAL_RUNNING;

        callback_keyboard(gly);
        if (gly->error_string) break;

        callback_loop(gly);
        if (gly->error_string) break;

        if (gly->frameskip_count++ >= gly->frameskip) {
            gly->frameskip_count = 0;
            callback_draw(gly);
        }
        if (gly->error_string) break;
    }
    while(0);

    do {
        should_close = gly->error_string != 0;
        if (should_close) break;

        gly_hook_should_close(&should_close);
        if (should_close) break;

        should_close = gly->internal & GECND_INTERNAL_WANT_EXIT;
        if (should_close) break;

        should_close = gecnd_signal != 0;
        if (should_close) break;
    }
    while(0);

    return !should_close;
}
