#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>

#include "gehook.h"
#include "gecnd.h"
#include "gamely_media.h"

static int lua_native_media_bootstrap(lua_State *L) {
    lua_pushinteger(L, 1);
    return 1;
}

static int lua_native_media_source(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    const char *url = luaL_checkstring(L, 2);
    if (channel == 0) gamely_daemon_media_playback_stop(channel);
    gamely_daemon_media_playback_source(channel, url);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_position(lua_State *L) {
    uint8_t  channel = (uint8_t) luaL_checkinteger(L, 1);
    int16_t  x       = (int16_t) luaL_checknumber(L, 2);
    int16_t  y       = (int16_t) luaL_checknumber(L, 3);
    int16_t  w       = (int16_t) luaL_checknumber(L, 4);
    int16_t  h       = (int16_t) luaL_checknumber(L, 5);
    gamely_daemon_media_playback_position(channel, x, y, w, h);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_play(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gamely_daemon_media_playback_play(channel);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_pause(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gamely_daemon_media_playback_pause(channel);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_stop(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gamely_daemon_media_playback_stop(channel);
    lua_settop(L, 0);
    return 0;
}

const luaL_Reg frontend_api_media[] = {
    {"native_media_bootstrap", lua_native_media_bootstrap},
    {"native_media_source",    lua_native_media_source},
    {"native_media_position",  lua_native_media_position},
    {"native_media_play",      lua_native_media_play},
    {"native_media_pause",     lua_native_media_pause},
    {"native_media_stop",      lua_native_media_stop},
    {NULL, NULL}
};
