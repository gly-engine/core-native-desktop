#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "gecnd.h"

static gecnd_enum_error_code_t file_loader(lua_State *L, int* ref, const char *const path, const char *const file)
{
    if (!ref) return GECND_ERROR_NO_GLY;
    if (!path) return GECND_ERROR_NO_PATH_STR;
    if (!file) return GECND_ERROR_NO_FILE_STR;

    size_t len_path = strlen(path);
    size_t len_file = strlen(file);
    size_t total_len = len_path + 1 + len_file + 1;
    char *full_path = (char *) malloc(total_len);
    if (!full_path) return GECND_ERROR_MALLOC_FAIL;

    snprintf(full_path, total_len, "%s/%s", path, file);

    if (luaL_loadfile(L, full_path) != LUA_OK) {
        free(full_path);
        return GECND_ERROR_NO_GAME;
    }

    free(full_path);

    if (*ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, *ref);
        *ref = LUA_NOREF;
    }

    *ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return GECND_OK;
}

gecnd_enum_error_code_t gecnd_set_game_file(gecnd_t *gly, const char *const path, const char *const file)
{
    if (!gly || !gly->L) return GECND_ERROR_NO_GLY;
    return file_loader(gly->L, &gly->ref_code_game, path, file);
}

gecnd_enum_error_code_t gecnd_set_engine_file(gecnd_t *gly, const char *const path, const char *const file)
{
    if (!gly || !gly->L) return GECND_ERROR_NO_GLY;
    return file_loader(gly->L, &gly->ref_code_engine, path, file);
}
