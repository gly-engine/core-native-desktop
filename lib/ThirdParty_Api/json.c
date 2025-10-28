#include <lauxlib.h>
#include <lua.h>

#include "gehook.h"

extern int luaopen_cjson(lua_State *L);

void gly_hook_luaopen_cjson(lua_State *L) {
    printf("carregando jeison\n");

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
}
