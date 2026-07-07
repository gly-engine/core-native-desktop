#define LUA_API     extern "C"
#define LUACODE_API extern "C"

#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

extern "C" {

typedef const char *(*lua_Reader)(lua_State *L, void *ud, size_t *size);

void lua_register(lua_State *L, const char *name, lua_CFunction f) {
    lua_pushcfunction(L, f, name);
    lua_setglobal(L, name);
}

int luaL_loadbuffer(lua_State *L, const char *buff, size_t size, const char *name) {
    lua_CompileOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.optimizationLevel = 2;
    opts.debugLevel = 0;

    size_t bc_size = 0;
    char *bc = luau_compile(buff, size, &opts, &bc_size);
    if (!bc) {
        lua_pushstring(L, "luau_compile: out of memory");
        return LUA_ERRMEM;
    }

    int status = luau_load(L, name ? name : "=?", bc, bc_size, 0);
    free(bc);
    return status;
}

int luaL_loadstring(lua_State *L, const char *s) {
    return luaL_loadbuffer(L, s, strlen(s), s);
}

int luaL_dostring(lua_State *L, const char *s) {
    int status = luaL_loadstring(L, s);
    if (status) return status;
    return lua_pcall(L, 0, LUA_MULTRET, 0);
}

int lua_load(lua_State *L, lua_Reader reader, void *data, const char *chunkname) {
    char  *buf = NULL;
    size_t len = 0;
    size_t cap = 0;

    for (;;) {
        size_t n = 0;
        const char *chunk = reader(L, data, &n);
        if (!chunk || n == 0) break;
        if (len + n > cap) {
            cap = (len + n) * 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) {
                free(buf);
                lua_pushstring(L, "lua_load: out of memory");
                return LUA_ERRMEM;
            }
            buf = tmp;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }

    int status = luaL_loadbuffer(L, buf ? buf : "", len, chunkname);
    free(buf);
    return status;
}

int luaL_ref(lua_State *L, int t) {
    (void)t;
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    return ref;
}

void luaL_unref(lua_State *L, int t, int ref) {
    (void)t;
    lua_unref(L, ref);
}

}
