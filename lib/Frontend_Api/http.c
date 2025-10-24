#include <lauxlib.h>
#include <lua.h>

static int hook_native_http_handler(lua_State *L) {
    lua_settop(L, 0);
    /// @todo
    return 0;
}

const luaL_Reg frontend_api_http[] = {{"native_http_handler", hook_native_http_handler},
    {NULL, NULL}};
