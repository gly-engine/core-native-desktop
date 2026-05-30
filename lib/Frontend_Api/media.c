#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>

#include "gehook.h"
#include "gecnd.h"


static int lua_native_media_bootstrap(lua_State *L) {
    lua_pushinteger(L, 1);
    return 1;
}

static int lua_native_media_source(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    const char *url = luaL_checkstring(L, 2);
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
    gamely_daemon_media_playback_command(channel, GDMSP_CMD_PLAY);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_pause(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gamely_daemon_media_playback_command(channel, GDMSP_CMD_PAUSE);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_stop(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gamely_daemon_media_playback_command(channel, GDMSP_CMD_STOP);
    lua_settop(L, 0);
    return 0;
}

static const char *fsm_to_str(gdmsp_fsm_t st) {
    switch (st) {
        case GDMSP_FSM_IDLE:     return "idle";
        case GDMSP_FSM_OPENING:  return "opening";
        case GDMSP_FSM_LOADING:  return "loading";
        case GDMSP_FSM_PLAYING:  return "playing";
        case GDMSP_FSM_PAUSED:   return "paused";
        case GDMSP_FSM_STOPPING: return "stopping";
        case GDMSP_FSM_ERROR:    return "error";
        default:                 return "idle";
    }
}

static int lua_native_media_get_status(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_fsm_t st  = gamely_daemon_media_playback_get_status(channel);
    lua_settop(L, 0);
    lua_pushstring(L, fsm_to_str(st));
    return 1;
}

static int lua_native_media_get_integer(lua_State *L) {
    uint8_t     channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_cmd_t cmd     = (gdmsp_cmd_t)luaL_checkinteger(L, 2);
    int64_t     value   = gamely_daemon_media_playback_get_integer(channel, cmd);
    lua_settop(L, 0);
    lua_pushinteger(L, (lua_Integer)value);
    return 1;
}

static int lua_native_media_set_integer(lua_State *L) {
    uint8_t     channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_cmd_t cmd     = (gdmsp_cmd_t)luaL_checkinteger(L, 2);
    int64_t     value   = (int64_t)luaL_checkinteger(L, 3);
    gdmsp_fsm_t st      = gamely_daemon_media_playback_set_integer(channel, cmd, value);
    lua_settop(L, 0);
    lua_pushstring(L, fsm_to_str(st));
    return 1;
}

const luaL_Reg frontend_api_media[] = {
    {"native_media_bootstrap", lua_native_media_bootstrap},
    {"native_media_source",    lua_native_media_source},
    {"native_media_position",  lua_native_media_position},
    {"native_media_play",      lua_native_media_play},
    {"native_media_pause",     lua_native_media_pause},
    {"native_media_stop",      lua_native_media_stop},
    {"native_media_get_status",  lua_native_media_get_status},
    {"native_media_get_integer", lua_native_media_get_integer},
    {"native_media_set_integer", lua_native_media_set_integer},
    {NULL, NULL}
};
