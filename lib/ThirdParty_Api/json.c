#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gehook.h"

extern int luaopen_cjson(lua_State *L);

static int lua_native_json_open(lua_State *L) {
    luaopen_cjson(L);
    int cjson_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, cjson_ref); 
    lua_pushstring(L, "encode_empty_table_as_object");
    lua_gettable(L, -2);
    lua_pushboolean(L, 0);
    lua_pcall(L, 1, 0, 0); 
    lua_pop(L, 1);

    lua_rawgeti(L, LUA_REGISTRYINDEX, cjson_ref);
    lua_pushstring(L, "encode_skip_unsupported_value_types");
    lua_gettable(L, -2);
    lua_pushboolean(L, 1);
    lua_pcall(L, 1, 0, 0);
    lua_pop(L, 1);

    lua_rawgeti(L, LUA_REGISTRYINDEX, cjson_ref);
    lua_pushstring(L, "encode");
    lua_gettable(L, -2);
    lua_setglobal(L, "native_json_encode");
    lua_pop(L, 1);

    lua_rawgeti(L, LUA_REGISTRYINDEX, cjson_ref);
    lua_pushstring(L, "decode");
    lua_gettable(L, -2);
    lua_setglobal(L, "native_json_decode");
    lua_pop(L, 1);
    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_init:native_json_open", lua_native_json_open, NULL);
}
