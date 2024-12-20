#include "zeebo.h"

static uint8_t previous_font_size = 5;
static uint8_t font_size = 5;

static int native_text_mock(lua_State *L)
{
    lua_settop(L, 0);
    return 0;
}

/**
 * @param[in] x @c double
 * @param[in] y @c double
 * @param[in] message @c string
 */
static int native_text_print(lua_State *L)
{
    uint32_t x = luaL_checknumber(L, 1);
    uint32_t y = luaL_checknumber(L, 2);
    const char* text = luaL_checkstring(L, 3);
    sdl_text_print(0, x, y, text, NULL, NULL);
    lua_settop(L, 0);
    return 0;
}

/**
 * @param[in] message @c string
 * @retval width @c double
 * @retval height @c double
 */
static int native_text_mensure(lua_State *L)
{
    uint32_t width, height;
    const char* text = luaL_checkstring(L, 3);
    sdl_text_print(1, 0, 0, text, &width, &height);
    lua_settop(L, 0);
    lua_pushnumber(L, width);
    lua_pushnumber(L, height);
    return 2;
}

/**
 * @param[in] size @c int
 */
static int native_text_font_size(lua_State *L)
{
    previous_font_size = font_size;
    sdl_text_font_size(luaL_checknumber(L, 1));
    lua_settop(L, 0);
    return 0;
}

static int native_text_font_previous(lua_State *L)
{
    sdl_text_font_previous();
    lua_settop(L, 0);
    return 0;
}

void native_text_load()
{
    int i = 0;
    lua_State *const L = lua();
    
    static const luaL_Reg lib[] = {
        {"native_text_print", native_text_print},
        {"native_text_mensure", native_text_mensure},
        {"native_text_font_size", native_text_font_size},
        {"native_text_font_name", native_text_mock},
        {"native_text_font_default", native_text_mock},
        {"native_text_font_previous", native_text_font_previous},
    };

    while(i < sizeof(lib)/sizeof(luaL_Reg)) {
        lua_pushcfunction(L, lib[i].func);
        lua_setglobal(L, lib[i].name);
        i = i + 1;
    }
}

void native_text_install() {
    kernel_event_install(KERNEL_EVENT_PRE_INIT, native_text_load);
}
