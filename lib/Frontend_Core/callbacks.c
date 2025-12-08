#include <string.h>
#include <stdlib.h>
#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gehook.h"

gecnd_enum_error_code_t gecnd_native_callback_init(gecnd_t *gly, int16_t width, int16_t height) {
    gly_hook_display_init(width, height);

    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_code_engine);
    if (lua_pcall(gly->L, 0, 0, 0) != LUA_OK) {
        printf("lua error: %s", luaL_checkstring(gly->L, -1));
        exit(1);
    }

    if (lua_getglobal(gly->L, "native_callback_init") != LUA_TFUNCTION) {
        printf("native_callback_init");
        exit(1);
    }

    lua_pushnumber(gly->L, width);
    lua_pushnumber(gly->L, height);

    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_code_game);
    if(lua_pcall(gly->L, 0, 1, 0) != LUA_OK) {
        printf("lua error: %s", luaL_checkstring(gly->L, -1));
        exit(1);
    }

    if(lua_pcall(gly->L, 3, 0, 0) != LUA_OK) {
        printf("lua error: %s", luaL_checkstring(gly->L, -1));
        exit(1);
    }

    if (lua_getglobal(gly->L, "native_callback_draw") != LUA_TFUNCTION) {
        printf("native_callback_draw");
        exit(1);
    }
    gly->ref_native_callback_draw = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    if (lua_getglobal(gly->L, "native_callback_loop") != LUA_TFUNCTION) {
        printf("native_callback_loop");
        exit(1);
    }
    gly->ref_native_callback_loop = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    if (lua_getglobal(gly->L, "native_callback_keyboard") != LUA_TFUNCTION) {
        printf("native_callback_keyboard");
        exit(1);
    }
    gly->ref_native_callback_keyboard = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    return GECND_OK;
}

gecnd_enum_error_code_t gecnd_native_callback_draw(gecnd_t *gly) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_draw);
    if (lua_pcall(gly->L, 0, 0, 0) != LUA_OK) {
        printf("lua error: %s\n", luaL_checkstring(gly->L, -1));
    }
    return GECND_OK;
}

gecnd_enum_error_code_t gecnd_native_callback_loop(gecnd_t *gly, int16_t delta_time) {
    gly->key_index = 0;
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_loop);
    lua_pushnumber(gly->L, delta_time);
    if (lua_pcall(gly->L, 1, 0, 0) != LUA_OK) {
        printf("lua error: %s\n", luaL_checkstring(gly->L, -1));
    }
    return GECND_OK;
}

gecnd_enum_error_code_t gecnd_native_callback_keyboard(gecnd_t *gly, char *key, bool pressed) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
    lua_pushstring(gly->L, key);
    lua_pushboolean(gly->L, pressed);
    if (lua_pcall(gly->L, 2, 0, 0) != LUA_OK) {
        printf("lua error: %s\n", luaL_checkstring(gly->L, -1));
    }
    return GECND_OK;
}
