#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"

bool option_disable_radius = false;

GECND_NATIVE_STUB(native_draw_start, ());
GECND_NATIVE_STUB(native_draw_flush, ());
GECND_NATIVE_STUB(native_draw_color, (uint32_t));
GECND_NATIVE_STUB(native_draw_clear, (uint32_t));
GECND_NATIVE_STUB(native_draw_rect, (uint8_t, int16_t, int16_t, int16_t, int16_t, int16_t));
GECND_NATIVE_STUB(native_draw_line, (int16_t, int16_t, int16_t, int16_t));

static int lua_native_draw_start(lua_State *L) {
    (void) L;
    native_draw_start();
    return 0;
}

static int lua_native_draw_flush(lua_State *L) {
    (void) L;
    return 0;
}

static int lua_native_draw_clear(lua_State *L) {
    uint32_t color = (uint32_t) lua_tonumber(L, 1);
    native_draw_clear(color);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_draw_color(lua_State *L) {
    uint32_t color = (uint32_t) lua_tonumber(L, 1);
    native_draw_color(color);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_draw_rect(lua_State *L) {
    uint8_t mode = luaL_checkinteger(L, 1);
    int16_t x = (int16_t) luaL_checknumber(L, 2);
    int16_t y = (int16_t) luaL_checknumber(L, 3);
    int16_t w = (int16_t) luaL_checknumber(L, 4);
    int16_t h = (int16_t) luaL_checknumber(L, 5);
    int16_t r = (int16_t) ((option_disable_radius && lua_gettop(L) >= 6 && !lua_isnil(L, 6))? luaL_checknumber(L, 6): 0);
    native_draw_rect(mode, x, y, w, h, r);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_draw_line(lua_State *L) {
    int16_t x1 = (int16_t) luaL_checknumber(L, 1);
    int16_t y1 = (int16_t) luaL_checknumber(L, 2);
    int16_t x2 = (int16_t) luaL_checknumber(L, 3);
    int16_t y2 = (int16_t) luaL_checknumber(L, 4);
    native_draw_line(x1, y1, x2, y2);
    lua_settop(L, 0);
    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_draw_start", lua_native_draw_start, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_flush", lua_native_draw_flush, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_clear", lua_native_draw_clear, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_color", lua_native_draw_color, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_rect2", lua_native_draw_rect, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_rect", lua_native_draw_rect, NULL);
    gecnd_registry("set", "lua_global_func:native_draw_line", lua_native_draw_line, NULL);
    gecnd_registry("bind", "backend_global:native_draw_start", &native_draw_start, (void*) GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_draw_flush", &native_draw_flush, (void*) GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_draw_color", &native_draw_color, (void*) GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_draw_rect", &native_draw_rect, (void*) GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_draw_line", &native_draw_line, (void*) GECND_TYPE_VOID);
    gecnd_registry("bind", "option:disable_radius", &option_disable_radius, (void*) GECND_TYPE_BOOLEAN);
}
