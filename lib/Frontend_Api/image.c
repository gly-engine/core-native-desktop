#include <string.h>
#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif
#include "gecnd.h"

GECND_NATIVE_DAEMON(native_image_load, (const char *url, int32_t *out_id));
GECND_NATIVE_DAEMON(native_image_exists, (int32_t id, bool *out_exists));
GECND_NATIVE_DAEMON(native_image_draw, (int32_t id, int16_t x, int16_t y));
GECND_NATIVE_DAEMON(native_image_mensure, (int32_t id, int16_t *out_w, int16_t *out_h));
GECND_NATIVE_DAEMON(native_image_error, (int32_t id, const char **out_err));
GECND_NATIVE_DAEMON(native_image_unload, (int32_t id, const char *url));
GECND_NATIVE_DAEMON(native_image_unload_all, (void));
GECND_NATIVE_DAEMON(native_image_loading_count, (int32_t *out_count));

static int32_t resolve_id(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TNUMBER)
        return (int32_t)lua_tointeger(L, idx);
    if (lua_type(L, idx) == LUA_TSTRING) {
        int32_t id = -1;
        native_image_load(lua_tostring(L, idx), &id);
        return id;
    }
    return -1;
}

static void daemon_native_image_load(const char *url, int32_t *out_id) {
    *out_id = gamely_daemon_img_get_id(url);
}

static void daemon_native_image_exists(int32_t id, bool *out_exists) {
    *out_exists = gamely_daemon_img_get_state(id) == GLY_IMG_READY;
}

static void daemon_native_image_draw(int32_t id, int16_t x, int16_t y) {
    gamely_daemon_img_draw(id, x, y);
}

static void daemon_native_image_mensure(int32_t id, int16_t *out_w, int16_t *out_h) {
    gamely_daemon_img_get_mensure(id, out_w, out_h);
}

static void daemon_native_image_error(int32_t id, const char **out_err) {
    *out_err = gamely_daemon_img_get_error(id);
}

static void daemon_native_image_unload(int32_t id, const char *url) {
    if (url) gamely_daemon_img_unload_url(url);
    else     gamely_daemon_img_unload_id(id);
}

static void daemon_native_image_unload_all(void) {
    gamely_daemon_img_unload_all();
}

static void daemon_native_image_loading_count(int32_t *out_count) {
    *out_count = gamely_daemon_img_loading_count();
}

static int lua_native_image_load(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    int32_t id = -1;
    native_image_load(url, &id);
    lua_pushinteger(L, id);
    return 1;
}

static int lua_native_image_exists(lua_State *L) {
    int32_t id = resolve_id(L, 1);
    bool exists = false;
    if (id != -1) native_image_exists(id, &exists);
    lua_pushboolean(L, exists);
    return 1;
}

static int lua_native_image_draw(lua_State *L) {
    int32_t id = resolve_id(L, 1);
    int16_t x  = (int16_t)luaL_checknumber(L, 2);
    int16_t y  = (int16_t)luaL_checknumber(L, 3);
    if (id != -1) native_image_draw(id, x, y);
    lua_pushinteger(L, id);
    return 1;
}

static int lua_native_image_mensure(lua_State *L) {
    int32_t id   = resolve_id(L, 1);
    int16_t w = 0, h = 0;
    if (id != -1) native_image_mensure(id, &w, &h);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int lua_native_image_error(lua_State *L) {
    int32_t     id  = resolve_id(L, 1);
    const char *err = NULL;
    if (id != -1) native_image_error(id, &err);
    if (err) lua_pushstring(L, err);
    else     lua_pushnil(L);
    return 1;
}

static int lua_native_image_unload(lua_State *L) {
    if (lua_type(L, 1) == LUA_TSTRING)
        native_image_unload(-1, lua_tostring(L, 1));
    else if (lua_type(L, 1) == LUA_TNUMBER)
        native_image_unload((int32_t)lua_tointeger(L, 1), NULL);
    return 0;
}

static int lua_native_image_unload_all(lua_State *L) {
    (void)L;
    native_image_unload_all();
    return 0;
}

static int lua_native_image_loading_count(lua_State *L) {
    int32_t count = 0;
    native_image_loading_count(&count);
    lua_pushinteger(L, count);
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
    gecnd_registry("bind", "backend_func:native_image_load", &native_image_load, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_image_exists", &native_image_exists, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_image_draw", &native_image_draw, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_image_mensure", &native_image_mensure, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_image_error", &native_image_error, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_image_unload", &native_image_unload, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_image_unload_all", &native_image_unload_all, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_image_loading_count", &native_image_loading_count, GECND_TYPE_VOID);
}

__attribute__((destructor))
static void cleanup() {
    gamely_daemon_img_unload_all();
}
