#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"
#include "gehook.h"

static bool option_mojibake = false;
static float option_font_factor = 1.0;

GECND_NATIVE_STUB(native_text_print, (int16_t, int16_t, const char *));
GECND_NATIVE_STUB(native_text_mensure, (const char *, int16_t *, int16_t *));
GECND_NATIVE_STUB(native_text_font_size, (uint8_t));
GECND_NATIVE_STUB(native_text_font_name, (const char *));
GECND_NATIVE_STUB(native_text_font_default, (uint8_t));
GECND_NATIVE_STUB(native_text_font_previous, ());

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
    int16_t x = (int16_t) luaL_checknumber(L, 1);
    int16_t y = (int16_t) luaL_checknumber(L, 2);
    if (option_mojibake) text = mojibake_dup(text);
    native_text_print(x, y, text);
    if (option_mojibake) free(text);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_mensure(lua_State *L) {
    int16_t w = 0, h = 0;
    const char *text = luaL_checkstring(L, 1);
    if (option_mojibake) text = mojibake_dup(text);
    native_text_mensure(text, &w, &h);
    if (option_mojibake) free(text);
    lua_settop(L, 0);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

static int lua_native_text_font_size(lua_State *L) {
    int16_t font_size = (int16_t) (floorf(luaL_checknumber(L, 1)) * option_font_factor);
    native_text_font_size(font_size);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_font_name(lua_State *L) {
    const char *font_name = luaL_checkstring(L, 1);
    native_text_font_name(font_name);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_font_default(lua_State *L) {
    int16_t font_default = luaL_checkinteger(L, 1);
    native_text_font_default(font_default);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_text_font_previous(lua_State *L) {
    ///! @todo remove on 0.4.X native_text_font_previous();
    lua_settop(L, 0);
    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_text_print", lua_native_text_print, NULL);
    gecnd_registry("set", "lua_global_func:native_text_mensure", lua_native_text_mensure, NULL);
    gecnd_registry("set", "lua_global_func:native_text_font_size", lua_native_text_font_size, NULL);
    gecnd_registry("set", "lua_global_func:native_text_font_name", lua_native_text_font_name, NULL);
    gecnd_registry("set", "lua_global_func:native_text_font_default", lua_native_text_font_default, NULL);
    gecnd_registry("set", "lua_global_func:native_text_font_previous", lua_native_text_font_previous, NULL);
    gecnd_registry("bind", "backend_func:native_text_print", &native_text_print, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_text_mensure", &native_text_mensure, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_text_font_size", &native_text_font_size, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_text_font_name", &native_text_font_name, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_text_font_default", &native_text_font_default, GECND_TYPE_VOID);
    gecnd_registry("bind", "backend_func:native_text_font_previous", &native_text_font_previous, GECND_TYPE_VOID);
    gecnd_registry("bind", "option:font_factor", &option_font_factor, GECND_TYPE_F32);
    gecnd_registry("bind", "option:mojibake", &option_mojibake, GECND_TYPE_BOOLEAN);
}

__attribute__((destructor))
static void cleanup() {
    /**
     * @todo unload all fonts
     */
}
