#include <lauxlib.h>
#include <lua.h>
#include <math.h>
#include <stdint.h>

#include "gecnd.h"

//! @cond
#define GLY_HOOK_TEMPLATE
#include "Frontend_Graphics/hooks.c"
//! @endcond

static int hook_native_draw_start(lua_State *L) {
    native_draw_start();
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_flush(lua_State *L) {
    native_draw_flush();
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_clear(lua_State *L) {
    native_draw_clear(luaL_checkinteger(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_color(lua_State *L) {
    native_draw_color(luaL_checkinteger(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_rect(lua_State *L) {
    native_draw_rect(luaL_checkinteger(L, 1), (int16_t)luaL_checknumber(L, 2), (int16_t)luaL_checknumber(L, 3), (int16_t)luaL_checknumber(L, 4),
                     (int16_t)luaL_checknumber(L, 5));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_draw_line(lua_State *L) {
    native_draw_line((int16_t)luaL_checknumber(L, 1), (int16_t)luaL_checknumber(L, 2), (int16_t)luaL_checknumber(L, 3),
                     (int16_t)luaL_checknumber(L, 4));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_print(lua_State *L) {
    native_text_print((int16_t)luaL_checknumber(L, 1), (int16_t)luaL_checknumber(L, 2), luaL_checkstring(L, 3));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_mensure(lua_State *L) {
    int16_t w = 1, h = 1;
    native_text_mensure(luaL_checkstring(L, 1), &w, &h);
    lua_settop(L, 0);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

static int hook_native_text_font_size(lua_State *L) {
    native_text_font_size(floorf((int16_t)luaL_checknumber(L, 1)));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_font_name(lua_State *L) {
    native_text_font_name(luaL_checkstring(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_font_default(lua_State *L) {
    native_text_font_default(luaL_checkinteger(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int hook_native_text_font_previous(lua_State *L) {
    native_text_font_previous();
    lua_settop(L, 0);
    return 0;
}

static const struct {
    const char *name;
    lua_CFunction func;
} glylibs[] = {{"native_draw_start", hook_native_draw_start},
               {"native_draw_flush", hook_native_draw_flush},
               {"native_draw_clear", hook_native_draw_clear},
               {"native_draw_color", hook_native_draw_color},
               {"native_draw_rect", hook_native_draw_rect},
               {"native_draw_line", hook_native_draw_line},
               {"native_text_print", hook_native_text_print},
               {"native_text_mensure", hook_native_text_mensure},
               {"native_text_font_size", hook_native_text_font_size},
               {"native_text_font_name", hook_native_text_font_name},
               {"native_text_font_default", hook_native_text_font_default},
               {"native_text_font_previous", hook_native_text_font_previous},
               {NULL, NULL}};

gecnd_enum_error_code_t gecnd_lua_open_graphics(gecnd_t *gly) {
    for (int i = 0; glylibs[i].name != NULL; i++) { 
        lua_register(gly->L, glylibs[i].name, glylibs[i].func);
    }
}