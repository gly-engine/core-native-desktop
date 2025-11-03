#include <stdbool.h>
#include <lauxlib.h>
#include <lua.h>

static int lua_native_http_handler(lua_State* L)
{
    lua_getfield(L, 1, "set");
    lua_pushstring(L, "body");
    lua_pushstring(L, "");
    lua_pcall(L, 2, 0, 0);

    lua_getfield(L, 1, "set");
    lua_pushstring(L, "ok");
    lua_pushboolean(L, 0);
    lua_pcall(L, 2, 0, 0);

    lua_getfield(L, 1, "set");
    lua_pushstring(L, "error");
    lua_pushstring(L, "mock http!");
    lua_pcall(L, 2, 0, 0);
    return 0;
}

void gly_hook_luaopen_http(lua_State* L)
{
    lua_pushcfunction(L, lua_native_http_handler);
    lua_setglobal(L, "native_http_handler");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_ssl");
}
