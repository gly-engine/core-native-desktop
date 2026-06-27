#include <stdio.h>

#include <lauxlib.h>
#include <lua.h>

#include "gehook.h"

static int lua_native_log_trace(lua_State *L) {
    const char *message = luaL_checkstring(L, 1);
    fprintf(stderr, "TRACE: %s\n", message);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_log_debug(lua_State *L) {
    const char *message = luaL_checkstring(L, 1);
    fprintf(stderr, "DEBUG: %s\n", message);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_log_info(lua_State *L) {
    const char *message = luaL_checkstring(L, 1);
    fprintf(stderr, "INFO: %s\n", message);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_log_warn(lua_State *L) {
    const char *message = luaL_checkstring(L, 1);
    fprintf(stderr, "WARNING: %s\n", message);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_log_error(lua_State *L) {
    const char *message = luaL_checkstring(L, 1);
    fprintf(stderr, "ERROR: %s\n", message);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_log_fatal(lua_State *L) {
    const char *message = luaL_checkstring(L, 1);
    fprintf(stderr, "FATAL: %s\n", message);
    lua_settop(L, 0);
    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_log_trace", lua_native_log_trace, NULL);
    gecnd_registry("set", "lua_global_func:native_log_info", lua_native_log_info, NULL);
    gecnd_registry("set", "lua_global_func:native_log_debug", lua_native_log_debug, NULL);
    gecnd_registry("set", "lua_global_func:native_log_warn", lua_native_log_warn, NULL);
    gecnd_registry("set", "lua_global_func:native_log_error", lua_native_log_error, NULL);
    gecnd_registry("set", "lua_global_func:native_log_fatal", lua_native_log_fatal, NULL);
}
