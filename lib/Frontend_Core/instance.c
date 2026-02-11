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

#ifndef GECND_DEFAULT_FPS
#define GECND_DEFAULT_FPS 60
#endif

#ifndef GECND_DEFAULT_FRAMESKIP
#define GECND_DEFAULT_FRAMESKIP 0
#endif

extern const luaL_Reg frontend_api_log[];
extern const luaL_Reg frontend_api_draw[];
extern const luaL_Reg frontend_api_text[];
extern const luaL_Reg frontend_api_image[];
extern const luaL_Reg frontend_api_media[];
extern const luaL_Reg frontend_api_system[];

static gecnd_t *instance;

void gecnd_global_signal_init();

gecnd_t *gecnd_new(lua_State* L) {
    gecnd_t *gly = NULL;
    bool native_keyboard_has_media = false;

    gecnd_global_signal_init();

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
        gly->target_fps = GECND_DEFAULT_FPS;
        gly->frameskip = GECND_DEFAULT_FRAMESKIP;
        gly->flags = GECND_FLAG_TIMER_PREFER_BACKEND;

        for (int i = 0; frontend_api_log[i].name != NULL; i++) {
            lua_register(L, frontend_api_log[i].name, frontend_api_log[i].func);
        }
        for (int i = 0; frontend_api_draw[i].name != NULL; i++) {
            lua_register(L, frontend_api_draw[i].name, frontend_api_draw[i].func);
        }
        for (int i = 0; frontend_api_text[i].name != NULL; i++) {
            lua_register(L, frontend_api_text[i].name, frontend_api_text[i].func);
        }
        for (int i = 0; frontend_api_media[i].name != NULL; i++) {
            lua_register(L, frontend_api_media[i].name, frontend_api_media[i].func);
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

    if(!instance) {
        instance = gly;
    }

    return gly;   
}

void gecnd_destroy(gecnd_t *gly) {
    if (gly) {
        gly_hook_luaclose_storage(gly->L);
        free(gly);
    }
}

gecnd_t *gecnd_get_root() {
    return instance;
}