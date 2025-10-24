#include <math.h>

#include <lauxlib.h>
#include <lua.h>

#include "gehook.h"

static int hook_native_image_load(lua_State *L) {
    /// @todo
    lua_settop(L, 0);
    return 0;
}

static int hook_native_image_draw(lua_State *L) {
    native_image_draw((int16_t)luaL_checknumber(L, 1), (int16_t)luaL_checknumber(L, 2), luaL_checkstring(L, 3));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_image_mensure(lua_State *L) {
    int16_t w = 1, h = 1;
    native_image_mensure(luaL_checkstring(L, 1), &w, &h);
    lua_settop(L, 0);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

const luaL_Reg frontend_api_image[] = {
               {"native_image_load", hook_native_image_load},
               {"native_image_draw", hook_native_image_draw},
               {"native_image_mensure", hook_native_image_mensure},
               {NULL, NULL}};
