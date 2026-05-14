#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>

#include "gecnd.h"
#include "gedll.h"

typedef int         (*fn_luaopen)(lua_State *L);
typedef void        (*fn_gecnd_open)(void);
typedef const char *(*fn_plugin_name)(void);

typedef struct gecnd_plugin_t {
    LIB_HANDLE          handle;
    fn_luaopen          luaopen;
    char                name[64];
    struct gecnd_plugin_t *next;
} gecnd_plugin_t;

static gecnd_plugin_t *plugin_head = NULL;

static void extract_module_name(const char *path, char *out, size_t out_size) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
#ifdef _WIN32
    const char *sep = strrchr(base, '\\');
    if (sep) base = sep + 1;
#endif
    if (strncmp(base, "lib", 3) == 0) base += 3;
    size_t i = 0;
    while (i < out_size - 1 && base[i] && base[i] != '.') {
        out[i] = base[i];
        i++;
    }
    out[i] = '\0';
}

bool gecnd_plugin_load(gecnd_t *gly, const char *path) {
    LIB_HANDLE lib = load_library(path);
    if (!lib) {
#ifndef _WIN32
        const char *dl_err = dlerror();
        fprintf(stderr, "[plugin] failed to load '%s': %s\n", path, dl_err ? dl_err : "unknown error");
#else
        fprintf(stderr, "[plugin] failed to load '%s'\n", path);
#endif
        gecnd_add_error(gly, "failed to load plugin: %s", path);
        return false;
    }

    char name[64];
    extract_module_name(path, name, sizeof(name));

    char sym[128];
    snprintf(sym, sizeof(sym), "coreopen_%s", name);
    fn_gecnd_open open_fn = (fn_gecnd_open)get_symbol(lib, sym);
    if (open_fn) open_fn();

    snprintf(sym, sizeof(sym), "luaopen_%s", name);
    fn_luaopen lua_fn = (fn_luaopen)get_symbol(lib, sym);

    gecnd_plugin_t *node = malloc(sizeof(gecnd_plugin_t));
    if (!node) {
        gecnd_add_error(gly, "out of memory");
        return false;
    }
    node->handle = lib;
    node->luaopen = lua_fn;
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->next = plugin_head;
    plugin_head = node;

    printf("[plugin] loaded '%s'\n", name);
    return true;
}

const char *gecnd_plugins_open_lua(lua_State *L) {
    for (gecnd_plugin_t *p = plugin_head; p; p = p->next) {
        if (!p->luaopen) continue;
        lua_pushcfunction(L, p->luaopen);
        if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
            return luaL_checkstring(L, -1);
        }
    }
    return NULL;
}
