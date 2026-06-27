#include <lua.h>
#include <lauxlib.h>
#include "gecnd.h"

static int lua_native_storage_set(lua_State *L) {
    const char *key   = luaL_checkstring(L, 1);
    const char *value = luaL_checkstring(L, 2);
    gamely_daemon_db_kv_set(key, value);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_storage_get(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    const char *value = gamely_daemon_db_kv_get(key);

    lua_pushvalue(L, 2);
    if (value)
        lua_pushstring(L, value);
    else
        lua_pushnil(L);
    lua_call(L, 1, 0);

    lua_settop(L, 0);
    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_storage_set", lua_native_storage_set, NULL);
    gecnd_registry("set", "lua_global_func:native_storage_get", lua_native_storage_get, NULL);
}

__attribute__((destructor))
static void cleanup() {
    /**
     * @todo safe shutdown database
     */
}
