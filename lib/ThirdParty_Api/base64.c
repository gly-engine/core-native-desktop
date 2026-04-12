#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include <libbase64.h>
#include "gehook.h"

static int lua_base64_encode(lua_State *L) {
    size_t input_len;
    const char *input = luaL_checklstring(L, 1, &input_len);

    size_t output_len = 4 * ((input_len + 2) / 3) + 4;
    char *output = (char *) malloc(output_len);
    if (!output) return luaL_error(L, "out of memory");

    size_t len = 0;
    base64_encode(input, input_len, output, &len, 0);

    lua_pushlstring(L, output, len);
    free(output);
    return 1;
}

static int lua_base64_decode(lua_State *L) {
    size_t input_len;
    const char *input = luaL_checklstring(L, 1, &input_len);

    size_t output_len = input_len * 3 / 4 + 4;
    char *output = (char *) malloc(output_len);
    if (!output) return luaL_error(L, "out of memory");

    size_t len = 0;
    int ret = base64_decode(input, input_len, output, &len, 0);

    if (ret <= 0) {
        free(output);
        return luaL_error(L, "invalid base64 input");
    }

    lua_pushlstring(L, output, len);
    free(output);
    return 1;
}

void gly_hook_luaopen_base64(lua_State *L) {
    const luaL_Reg base64_funcs[] = {
        {"native_base64_encode", lua_base64_encode},
        {"native_base64_decode", lua_base64_decode},
        {NULL, NULL}
    };
    
    for (int i = 0; base64_funcs[i].name != NULL; i++) {
        lua_register(L, base64_funcs[i].name, base64_funcs[i].func);
    }
}
