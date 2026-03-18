#include <lua.h>
#include <lauxlib.h>

#include "gehook.h"

static int lua_native_libretro_url(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    bool ok = native_libretro_url(url);
    lua_pushboolean(L, ok);
    return 1;
}

static int lua_native_libretro_exit(lua_State *L) {
    (void)L;
    native_libretro_exit();
    return 0;
}

const luaL_Reg frontend_api_libretro[] = {
    {"native_libretro_url",  lua_native_libretro_url},
    {"native_libretro_exit", lua_native_libretro_exit},
    {NULL, NULL}
};
