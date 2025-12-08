#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "gecnd.h"

void file_loader(gecnd_t *gly, int* ref, const char *const path, const char *const file)
{
    char *full_path = NULL;
    lua_State *L = gly->L;

    do {
        if (!path) {
            gly->error_string = "missing path";
            break;
        }
        if (!file)  {
            gly->error_string = "missing file";
            break;
        }

        size_t len_path = strlen(path);
        size_t len_file = strlen(file);
        size_t total_len = len_path + 1 + len_file + 1;
        
        full_path = (char *) malloc(total_len);
        if (!full_path) {
            gly->error_string = "malloc failed";
            break;
        }

        snprintf(full_path, total_len, "%s/%s", path, file);

        if (luaL_loadfile(L, full_path) != LUA_OK) {
            gly->error_string = luaL_checkstring(gly->L, -1);
            break;
        }

        if (*ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, *ref);
            *ref = LUA_NOREF;
        }

        *ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    while(0);

    if (full_path) {
        free(full_path);
    }    
}

void gecnd_set_game_file(gecnd_t *gly, const char *const path, const char *const file)
{
    if (!(gly && !gly->error_string)) return;
    file_loader(gly, &gly->ref_code_game, path, file);
}

void gecnd_set_engine_file(gecnd_t *gly, const char *const path, const char *const file)
{
    if (!(gly && !gly->error_string)) return;
    file_loader(gly, &gly->ref_code_engine, path, file);
}
