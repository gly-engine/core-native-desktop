#include <stddef.h>

#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"

extern const luaL_Reg frontend_api_draw[];
extern const luaL_Reg frontend_api_text[];
extern const luaL_Reg frontend_api_http[];
extern const luaL_Reg frontend_api_image[];

gecnd_enum_error_code_t gecnd_lua_openlibs(gecnd_t *gly) 
{
    for (int i = 0; frontend_api_draw[i].name != NULL; i++) {
        lua_register(gly->L, frontend_api_draw[i].name, frontend_api_draw[i].func);
    }
    for (int i = 0; frontend_api_text[i].name != NULL; i++) {
        lua_register(gly->L, frontend_api_text[i].name, frontend_api_text[i].func);
    }
    for (int i = 0; frontend_api_http[i].name != NULL; i++) {
        lua_register(gly->L, frontend_api_http[i].name, frontend_api_http[i].func);
    }
    for (int i = 0; frontend_api_image[i].name != NULL; i++) {
        lua_register(gly->L, frontend_api_image[i].name, frontend_api_image[i].func);
    }
    return GECND_OK;
}
