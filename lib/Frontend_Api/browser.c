/**
 * @todo move this?
 */
#include <lua.h>
#include <lauxlib.h>
#include "gecnd.h"
#include "gehook.h"

static int lua_native_browser_url(lua_State *L) {
    const char *url = lua_tostring(L, 1);
    native_browser_url(url);
    return 0;
}

static int lua_native_browser_exit(lua_State *L) {
    native_browser_exit();
    return 0;
}

const luaL_Reg frontend_api_browser[] = {
    {"native_browser_url", lua_native_browser_url},
    {"native_browser_exit", lua_native_browser_exit},
    {NULL, NULL}
};
