#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gehook.h"

#ifndef GECND_DEFAULT_WIDTH
#define GECND_DEFAULT_WIDTH 1280
#endif

#ifndef GECND_DEFAULT_HEIGHT
#define GECND_DEFAULT_HEIGHT 720
#endif

extern const luaL_Reg frontend_api_log[];
extern const luaL_Reg frontend_api_draw[];
extern const luaL_Reg frontend_api_text[];
extern const luaL_Reg frontend_api_image[];
extern const luaL_Reg frontend_api_system[];

gecnd_t *gecnd_new(lua_State* L) {
    gecnd_t *gly = NULL;
    bool native_keyboard_has_media = false;
    const char *const cwd = gecnd_utils_get_exe_cwd();

    do {
        if (!L) {
            break;
        }

        gly = (gecnd_t*) malloc(sizeof(gecnd_t));

        if (!gly) {
            break;
        }

        (void) memset(gly, 0, sizeof(gecnd_t));

        lua_pushlightuserdata(L, gly);
        lua_rawseti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);

        gly->L = L;
        gly->width = GECND_DEFAULT_WIDTH;
        gly->height = GECND_DEFAULT_HEIGHT;
        gecnd_set_game_file(gly, cwd, "game.lua");
        gecnd_set_engine_file(gly, cwd, "main.lua");

        for (int i = 0; frontend_api_log[i].name != NULL; i++) {
            lua_register(L, frontend_api_log[i].name, frontend_api_log[i].func);
        }
        for (int i = 0; frontend_api_draw[i].name != NULL; i++) {
            lua_register(L, frontend_api_draw[i].name, frontend_api_draw[i].func);
        }
        for (int i = 0; frontend_api_text[i].name != NULL; i++) {
            lua_register(L, frontend_api_text[i].name, frontend_api_text[i].func);
        }
        for (int i = 0; frontend_api_image[i].name != NULL; i++) {
            lua_register(L, frontend_api_image[i].name, frontend_api_image[i].func);
        }
        for (int i = 0; frontend_api_system[i].name != NULL; i++) {
            lua_register(L, frontend_api_system[i].name, frontend_api_system[i].func);
        }

        gly_hook_keyboard_has_media(&native_keyboard_has_media);
        if (native_keyboard_has_media) {
            lua_pushboolean(L, true);
            lua_setglobal(L, "native_keyboard_has_media");
        }

        gly_hook_luaopen_http(L);
        gly_hook_luaopen_cjson(L);
        gly_hook_luaopen_base64(L);
        gly_hook_luaopen_storage(L);
    }
    while(0);
    return gly;   
}

void gecnd_destroy(gecnd_t *gly) {
    if (gly) {
        gly_hook_luaclose_storage(gly->L);
        free(gly);
    }
}
