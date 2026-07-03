#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"
#include "gedll.h"

typedef void (*fn_gecnd_open)(gecnd_plugin_t *plugin);

static gecnd_api_t *plugin_require(const char *abi);
static bool         plugin_load(const char *module);

static gecnd_api_t PLUGIN_API = {
    .lang     = gecnd_lang,
    .registry = gecnd_registry,
};

static gecnd_plugin_t PLUGIN = {
    .require = plugin_require,
    .load    = plugin_load,
};

static LIB_HANDLE g_handle = NULL;
static char       g_base[64];

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

static gecnd_api_t *plugin_require(const char *abi) {
    (void)abi;
    return &PLUGIN_API;
}

static bool plugin_load(const char *module) {
    char sym[128];
    snprintf(sym, sizeof(sym), "coreopen_%s_%s", g_base, module);
    fn_gecnd_open open_fn = (fn_gecnd_open)get_symbol(g_handle, sym);
    if (!open_fn) {
        fprintf(stderr, "[plugin] module not found: %s\n", sym);
        return false;
    }
    open_fn(&PLUGIN);
    return true;
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

    g_handle = lib;
    strncpy(g_base, name, sizeof(g_base) - 1);
    g_base[sizeof(g_base) - 1] = '\0';
    char *us = strrchr(g_base, '_');
    if (us) *us = '\0';

    char sym[128];
    snprintf(sym, sizeof(sym), "coreopen_%s", name);
    fn_gecnd_open open_fn = (fn_gecnd_open)get_symbol(lib, sym);
    if (open_fn) open_fn(&PLUGIN);

    printf("[plugin] loaded '%s'\n", name);
    return true;
}

const char *gecnd_plugins_open_lua(lua_State *L) {
    (void)L;
    return NULL;
}
