#include <stdint.h>
#include <stdbool.h>
#include <lauxlib.h>
#include <lua.h>

#include "http_common.h"

/// @todo move to daemon WebClient

static int lua_native_http_handler(lua_State* L)
{
    //native_http_immediate_error(L, "mock http!");
    return 0;
}

void gly_hook_luaopen_http(lua_State* L)
{
    //lua_pushcfunction(L, lua_native_http_handler);
    //lua_setglobal(L, "native_http_handler");

    //lua_pushboolean(L, true);
    //lua_setglobal(L, "native_http_has_ssl");
}
