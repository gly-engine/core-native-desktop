#include <string.h>
#include <stdlib.h>
#include <lauxlib.h>
#include <lua.h>
#include "gecnd.h"

gecnd_enum_error_code_t gecnd_native_callback_init(gecnd_t *gly, int16_t width, int16_t height) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_code_engine);
    if (lua_pcall(gly->L, 0, 0, 0) != LUA_OK) {
        printf("lua error: %s", luaL_checkstring(gly->L, -1));
    }

    if (lua_getglobal(gly->L, "native_callback_init") != LUA_TFUNCTION) {
        printf("native_callback_init");
    }

    lua_pushnumber(gly->L, width);
    lua_pushnumber(gly->L, height);

    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_code_game);
    if(lua_pcall(gly->L, 0, 1, 0) != LUA_OK) {
        printf("lua error: %s", luaL_checkstring(gly->L, -1));
    }

    if(lua_pcall(gly->L, 3, 0, 0) != LUA_OK) {
        printf("lua error: %s", luaL_checkstring(gly->L, -1));
    }

    if (lua_getglobal(gly->L, "native_callback_draw") != LUA_TFUNCTION) {
        printf("native_callback_draw");
    }
    gly->ref_native_callback_draw = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    if (lua_getglobal(gly->L, "native_callback_loop") != LUA_TFUNCTION) {
        printf("native_callback_loop");
    }
    gly->ref_native_callback_loop = luaL_ref(gly->L, LUA_REGISTRYINDEX);

    gly_hook_display_init(width, height);
}

gecnd_enum_error_code_t gecnd_native_callback_draw(gecnd_t *gly) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_draw);
    if (lua_pcall(gly->L, 0, 0, 0) != LUA_OK) {
        printf("lua error: %s\n", luaL_checkstring(gly->L, -1));
    }
}

gecnd_enum_error_code_t gecnd_native_callback_loop(gecnd_t *gly, int16_t delta_time) {
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_loop);
    lua_pushnumber(gly->L, delta_time);
    if (lua_pcall(gly->L, 1, 0, 0) != LUA_OK) {
        printf("lua error: %s\n", luaL_checkstring(gly->L, -1));
    }
}

gecnd_enum_error_code_t gecnd_native_callback_keyboard(gecnd_t *gly, char *key, bool pressed) {

}
