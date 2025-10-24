#include <math.h>

#include <lauxlib.h>
#include <lua.h>

#include "gehook.h"

static int hook_native_text_print(lua_State *L) {
    native_text_print((int16_t)luaL_checknumber(L, 1), (int16_t)luaL_checknumber(L, 2), luaL_checkstring(L, 3));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_mensure(lua_State *L) {
    int16_t w = 1, h = 1;
    native_text_mensure(luaL_checkstring(L, 1), &w, &h);
    lua_settop(L, 0);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

static int hook_native_text_font_size(lua_State *L) {
    native_text_font_size(floorf((int16_t)luaL_checknumber(L, 1)));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_font_name(lua_State *L) {
    native_text_font_name(luaL_checkstring(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_font_default(lua_State *L) {
    native_text_font_default(luaL_checkinteger(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_font_previous(lua_State *L) {
    native_text_font_previous();
    lua_settop(L, 0);
    return 0;
}

const luaL_Reg frontend_api_text[] = {
               {"native_text_print", hook_native_text_print},
               {"native_text_mensure", hook_native_text_mensure},
               {"native_text_font_size", hook_native_text_font_size},
               {"native_text_font_name", hook_native_text_font_name},
               {"native_text_font_default", hook_native_text_font_default},
               {"native_text_font_previous", hook_native_text_font_previous},
               {NULL, NULL}};
