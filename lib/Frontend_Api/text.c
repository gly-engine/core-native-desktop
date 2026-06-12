#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gehook.h"

static const uint16_t cp1252_high[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static char *mojibake_dup(const char *src) {
    size_t len = 0;
    while (src[len]) len++;
    char *out = malloc(len * 3 + 1);
    if (!out) return NULL;
    char *w = out;
    for (size_t i = 0; i < len; i++) {
        unsigned char b = (unsigned char)src[i];
        uint16_t cp = (b >= 0x80 && b <= 0x9F) ? cp1252_high[b - 0x80] : b;
        if (cp < 0x80) {
            *w++ = (char)cp;
        } else if (cp < 0x800) {
            *w++ = (char)(0xC0 | (cp >> 6));
            *w++ = (char)(0x80 | (cp & 0x3F));
        } else {
            *w++ = (char)(0xE0 | (cp >> 12));
            *w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
            *w++ = (char)(0x80 | (cp & 0x3F));
        }
    }
    *w = '\0';
    return out;
}

static int lua_native_text_print(lua_State *L) {
    const char *text = luaL_checkstring(L, 3);
    char *fake = gecnd_get_display()->mojibake ? mojibake_dup(text) : NULL;
    native_text_print((int16_t)luaL_checknumber(L, 1), (int16_t)luaL_checknumber(L, 2), fake ? fake : text);
    free(fake);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_mensure(lua_State *L) {
    int16_t w = 1, h = 1;
    const char *text = luaL_checkstring(L, 1);
    char *fake = gecnd_get_display()->mojibake ? mojibake_dup(text) : NULL;
    native_text_mensure(fake ? fake : text, &w, &h);
    free(fake);
    lua_settop(L, 0);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

static int lua_native_text_font_size(lua_State *L) {
    float factor = gecnd_get_display()->font_factor;
    if (factor <= 0.0f) factor = 1.0f;
    native_text_font_size(floorf((int16_t)luaL_checknumber(L, 1) * factor));
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_font_name(lua_State *L) {
    native_text_font_name(luaL_checkstring(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_font_default(lua_State *L) {
    native_text_font_default(luaL_checkinteger(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_font_previous(lua_State *L) {
    native_text_font_previous();
    lua_settop(L, 0);
    return 0;
}

const luaL_Reg frontend_api_text[] = {
               {"native_text_print", lua_native_text_print},
               {"native_text_mensure", lua_native_text_mensure},
               {"native_text_font_size", lua_native_text_font_size},
               {"native_text_font_name", lua_native_text_font_name},
               {"native_text_font_default", lua_native_text_font_default},
               {"native_text_font_previous", lua_native_text_font_previous},
               {NULL, NULL}};
