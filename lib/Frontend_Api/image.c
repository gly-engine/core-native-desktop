#include <string.h>
#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif
#include "gecnd.h"

static int32_t resolve_id(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TNUMBER)
        return (int32_t)lua_tointeger(L, idx);
    if (lua_type(L, idx) == LUA_TSTRING)
        return gamely_daemon_img_get_id(lua_tostring(L, idx));
    return -1;
}

static int lua_native_image_load(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    int32_t id = gamely_daemon_img_get_id(url);
    lua_pushinteger(L, id);
    return 1;
}

static int lua_native_image_exists(lua_State *L) {
    int32_t id = resolve_id(L, 1);
    bool exists = gamely_daemon_img_get_state(id) == GLY_IMG_READY;
    lua_pushboolean(L, exists);
    return 1;
}

static int lua_native_image_draw(lua_State *L) {
    int32_t id = resolve_id(L, 1);
    int16_t x  = (int16_t)luaL_checknumber(L, 2);
    int16_t y  = (int16_t)luaL_checknumber(L, 3);
    if (id != -1) gamely_daemon_img_draw(id, x, y);
    lua_pushinteger(L, id);
    return 1;
}

static int lua_native_image_mensure(lua_State *L) {
    int32_t id   = resolve_id(L, 1);
    int16_t w = 0, h = 0;
    if (id != -1) gamely_daemon_img_get_mensure(id, &w, &h);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int lua_native_image_error(lua_State *L) {
    int32_t     id  = resolve_id(L, 1);
    const char *err = id != -1 ? gamely_daemon_img_get_error(id) : NULL;
    if (err) lua_pushstring(L, err);
    else     lua_pushnil(L);
    return 1;
}

static int lua_native_image_unload(lua_State *L) {
    if (lua_type(L, 1) == LUA_TSTRING)
        gamely_daemon_img_unload_url(lua_tostring(L, 1));
    else if (lua_type(L, 1) == LUA_TNUMBER)
        gamely_daemon_img_unload_id((int32_t)lua_tointeger(L, 1));
    return 0;
}

static int lua_native_image_unload_all(lua_State *L) {
    (void)L;
    gamely_daemon_img_unload_all();
    return 0;
}

static int lua_native_image_loading_count(lua_State *L) {
    lua_pushinteger(L, gamely_daemon_img_loading_count());
    return 1;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_image_load", lua_native_image_load, NULL);
    gecnd_registry("set", "lua_global_func:native_image_exists", lua_native_image_exists, NULL);
    gecnd_registry("set", "lua_global_func:native_image_draw", lua_native_image_draw, NULL);
    gecnd_registry("set", "lua_global_func:native_image_mensure", lua_native_image_mensure, NULL);
    gecnd_registry("set", "lua_global_func:native_image_error", lua_native_image_error, NULL);
    gecnd_registry("set", "lua_global_func:native_image_unload", lua_native_image_unload, NULL);
    gecnd_registry("set", "lua_global_func:native_image_unload_all", lua_native_image_unload_all, NULL);
    gecnd_registry("set", "lua_global_func:native_image_loading_count", lua_native_image_loading_count, NULL);
}

__attribute__((destructor))
static void cleanup() {
    gamely_daemon_img_unload_all();
}
