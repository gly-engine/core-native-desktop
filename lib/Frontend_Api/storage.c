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

void gly_hook_luaopen_storage(lua_State *L) {
    lua_register(L, "native_storage_set", lua_native_storage_set);
    lua_register(L, "native_storage_get", lua_native_storage_get);
}

void gly_hook_luaclose_storage(lua_State *L) { (void)L; }
