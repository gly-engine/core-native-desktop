#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gehook.h"

static int lua_native_draw_start(lua_State *L) {
    /** @todo use this? */
    (void) L;
    return 0;
}

static int lua_native_draw_flush(lua_State *L) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);
    gecnd_t *gly = lua_touserdata(L, -1);
    gly->want_blit = false;
    lua_settop(L, 0);
    return 0;
}

static int lua_native_draw_clear(lua_State *L) {
    uint32_t color = (uint32_t)lua_tonumber(L, 1);
    native_draw_clear(color);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_draw_color(lua_State *L) {
    uint32_t color = (uint32_t)lua_tonumber(L, 1);
    native_draw_color(color);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_draw_rect(lua_State *L) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);
    uint8_t mode = luaL_checkinteger(L, 1);
    int16_t x = (int16_t) luaL_checknumber(L, 2);
    int16_t y = (int16_t) luaL_checknumber(L, 3);
    int16_t w = (int16_t) luaL_checknumber(L, 4);
    int16_t h = (int16_t) luaL_checknumber(L, 5);
    bool radius = !gecnd_get_display()->disable_radius;
    float float_r = (float) ((radius && lua_gettop(L) >= 6 && !lua_isnil(L, 6))? luaL_checknumber(L, 6): 0);
    int16_t int_r = (int16_t) (int32_t) (float_r);
    native_draw_rect(mode, x, y, w, h, int_r);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_draw_line(lua_State *L) {
    native_draw_line((int16_t)luaL_checknumber(L, 1), (int16_t)luaL_checknumber(L, 2), (int16_t)luaL_checknumber(L, 3),
                     (int16_t)luaL_checknumber(L, 4));
    lua_settop(L, 0);
    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_draw_start", lua_native_draw_start, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_flush", lua_native_draw_flush, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_clear", lua_native_draw_clear, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_color", lua_native_draw_color, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_rect", lua_native_draw_rect, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_rect", lua_native_draw_rect, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_line", lua_native_draw_line, NULL);
}
