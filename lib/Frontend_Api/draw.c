#include <lauxlib.h>
#include <lua.h>

#include "gehook.h"

static int hook_native_draw_start(lua_State *L) {
    native_draw_start();
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_flush(lua_State *L) {
    native_draw_flush();
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_clear(lua_State *L) {
    native_draw_clear(luaL_checkinteger(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_color(lua_State *L) {
    native_draw_color(luaL_checkinteger(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_rect(lua_State *L) {
    native_draw_rect(luaL_checkinteger(L, 1), (int16_t)luaL_checknumber(L, 2), (int16_t)luaL_checknumber(L, 3), (int16_t)luaL_checknumber(L, 4),
                     (int16_t)luaL_checknumber(L, 5));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_line(lua_State *L) {
    native_draw_line((int16_t)luaL_checknumber(L, 1), (int16_t)luaL_checknumber(L, 2), (int16_t)luaL_checknumber(L, 3),
                     (int16_t)luaL_checknumber(L, 4));
    lua_settop(L, 0);
    return 0;
}

const luaL_Reg frontend_api_draw[] = {{"native_draw_start", hook_native_draw_start},
               {"native_draw_flush", hook_native_draw_flush},
               {"native_draw_clear", hook_native_draw_clear},
               {"native_draw_color", hook_native_draw_color},
               {"native_draw_rect", hook_native_draw_rect},
               {NULL, NULL}};
