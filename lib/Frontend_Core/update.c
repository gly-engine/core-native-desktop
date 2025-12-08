#include <string.h>
#include <stdlib.h>
#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gehook.h"

static void callback_init(gecnd_t *gly) {
    do {
        gly_hook_display_init(gly->width, gly->height);
        
        lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_code_engine);
        if (lua_pcall(gly->L, 0, 0, 0) != LUA_OK) {
            gly->error_string = luaL_checkstring(gly->L, -1);
            break;
        }
    
        if (lua_getglobal(gly->L, "native_callback_init") != LUA_TFUNCTION) {
            gly->error_string = "missing: native_callback_init";
            break;
        }

        lua_pushnumber(gly->L, gly->width);
        lua_pushnumber(gly->L, gly->height);

        lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_code_game);
        if(lua_pcall(gly->L, 0, 1, 0) != LUA_OK) {
            gly->error_string = luaL_checkstring(gly->L, -1);
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
    char* key = NULL;
    bool pressed = false;

    do {
        gly_hook_input_keyboard(index, &key, &pressed);

        if (!key && !pressed) break;
        index++;

        if (!key) break;

        lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
        lua_pushstring(gly->L, key);
        lua_pushboolean(gly->L, pressed);
        if (lua_pcall(gly->L, 2, 0, 0) != LUA_OK) {
            gly->error_string = luaL_checkstring(gly->L, -1);
            break;
        }
    }
    while(index < 100);
}

static void callback_loop(gecnd_t *gly) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_loop);
    lua_pushnumber(gly->L, gly->delta_time);
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

void gecnd_update(gecnd_t * gly)
{
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

        callback_draw(gly);
        if (gly->error_string) break;
    }
    while(0);
}
