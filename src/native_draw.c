#include "zeebo.h"

static int native_draw_start(lua_State *L) {
    sdl_draw_start();
    return 0;
}

static int native_draw_flush(lua_State *L) {
    sdl_draw_flush();
    return 0;
}

static int native_draw_clear(lua_State *L) {
    uint32_t color = luaL_checknumber(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    double w = luaL_checknumber(L, 4);
    double h = luaL_checknumber(L, 5);
    sdl_draw_clear(color, x, y, w, h);
    lua_settop(L, 0);
    return 0;
}

static int native_draw_color(lua_State *L) {
    uint32_t color = luaL_checknumber(L, 1);
    sdl_draw_color(color);
    lua_settop(L, 0);
    return 0;
}

static int native_draw_rect(lua_State *L) {
    uint8_t mode = luaL_checknumber(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    double w = luaL_checknumber(L, 4);
    double h = luaL_checknumber(L, 5);
    sdl_draw_rect(mode, x, y, w, h);
    lua_settop(L, 0);
    return 0;
}

static int native_draw_line(lua_State *L) {
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double x2 = luaL_checknumber(L, 3);
    double y2 = luaL_checknumber(L, 4);
    sdl_draw_line(x1, y1, x2, y2);
    lua_settop(L, 0);
    return 0;
}

static int native_draw_image(lua_State *L) {
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    const char* src = luaL_checkstring(L, 1);
    ffmpeg_image_draw(x, y, src);
    lua_settop(L, 0);
    return 0;
}

static void native_draw_load() {
    int i = 0;
    lua_State *const L = lua();

    static const luaL_Reg lib[] = {{"native_draw_start", native_draw_start},   {"native_draw_flush", native_draw_flush},
                                   {"native_draw_clear", native_draw_clear},   {"native_draw_color", native_draw_color},
                                   {"native_draw_rect", native_draw_rect},     {"native_draw_line", native_draw_line},
                                   {"native_draw_image", native_draw_image}};

    while (i < sizeof(lib) / sizeof(luaL_Reg)) {
        lua_pushcfunction(L, lib[i].func);
        lua_setglobal(L, lib[i].name);
        i = i + 1;
    }

    lua_newtable(L);
    lua_pushstring(L, "repeats");
    lua_newtable(L);
    lua_pushboolean(L, 1);
    lua_seti(L, -2, 1);
    lua_pushboolean(L, 1);
    lua_seti(L, -2, 2);
    lua_settable(L, -3);
    lua_pushstring(L, "line");
    lua_pushcfunction(L, native_draw_line);
    lua_settable(L, -3);
    lua_setglobal(L, "native_dict_poly");
}

void native_draw_install() {
    kernel_event_install(KERNEL_EVENT_PRE_INIT, native_draw_load);
}

#ifdef DOXYGEN
/**
 * @short @c std.draw.poly
 * @par Lua definition
 * @code
 * local native_dict_poly = {
 *  repeats = {true, true},
 *  line = function (x1, y1, x2, y2)
 *    native_draw_line(x1, y1, x2, y2)
 *  end
 * }
 * @endcode
 */
class native_dict_poly {
   public:
    bool repeats[2] = {true, true};
    /**
     * @param[in] x1 @c double
     * @param[in] y1 @c double
     * @param[in] x2 @c double
     * @param[in] y2 @c double
     */
    int line(lua_State *L);
};
#endif
