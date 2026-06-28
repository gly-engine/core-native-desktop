#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"

static void lua_global_func(const char *key, void *value, void *usr) {
    gecnd_t *const gly = (gecnd_t *)usr;
    lua_register(gly->L, key + sizeof("lua_global_func:") - 1, (lua_CFunction)value);
}

static void lua_global_init(const char *key, void *value, void *usr) {
    gecnd_t *const gly = (gecnd_t *)usr;
#ifdef LUAU_FASTMATH_BEGIN
    lua_pushcfunction(gly->L, (lua_CFunction)value, key);
#else
    lua_pushcfunction(gly->L, (lua_CFunction)value);
#endif
    if (lua_pcall(gly->L, 0, 0, 0)) {
        gecnd_add_error(gly, "[%s] %s", key, lua_tostring(gly->L, -1));
        lua_pop(gly->L, 1);
    }
}

static void lua_global_value(const char *key, void *value, void *usr) {
    gecnd_t *const gly = (gecnd_t *)usr;
}

static void boot_lua(const char* key, void* value, gecnd_t *gly) {
    gecnd_registry("get", "lua_global_func:*", lua_global_func, gly);
    gecnd_registry("get", "lua_global_init:*", lua_global_init, gly);
    gecnd_registry("get", "lua_global_value:*", lua_global_value, gly);
}

__attribute__((constructor))
static void init(void) {
    gecnd_registry("set", "boot:lua", (void *)boot_lua, NULL);
}
