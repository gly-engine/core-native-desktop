#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gehook.h"

extern const luaL_Reg frontend_api_draw[];
extern const luaL_Reg frontend_api_text[];
extern const luaL_Reg frontend_api_http[];
extern const luaL_Reg frontend_api_image[];
extern const luaL_Reg frontend_api_system[];

gecnd_t *gecnd_new(lua_State* L) {
    gecnd_t *gly = NULL;
    do {
        if (!L) {
            break;
        }

        gly = (gecnd_t*) malloc(sizeof(gecnd_t));

        if (!gly) {
            break;
        }

        gly->L = L;

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

        gly_hook_luaopen_cjson(L);
        gly_hook_luaopen_base64(L);
    }
    while(0);
    return gly;   
}

void gecnd_destroy(gecnd_t *gly) {
    if (gly) {
        free(gly);
    }
}
