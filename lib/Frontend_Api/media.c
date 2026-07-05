#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif
#include <string.h>
#include <stdio.h>

#include "gehook.h"
#include "gecnd.h"
#include "gdmsp.h"


static int lua_native_media_bootstrap(lua_State *L) {
    lua_pushinteger(L, 1);
    return 1;
}

static int lua_native_media_source(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    const char *url = luaL_checkstring(L, 2);
    gdmsp_control()->source(channel, url);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_position(lua_State *L) {
    uint8_t  channel = (uint8_t) luaL_checkinteger(L, 1);
    int16_t  x       = (int16_t) luaL_checknumber(L, 2);
    int16_t  y       = (int16_t) luaL_checknumber(L, 3);
    int16_t  w       = (int16_t) luaL_checknumber(L, 4);
    int16_t  h       = (int16_t) luaL_checknumber(L, 5);
    gdmsp_control()->position(channel, x, y, w, h);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_play(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_control()->set(channel, GDMSP_CMD_PLAY, NULL);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_pause(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_control()->set(channel, GDMSP_CMD_PAUSE, NULL);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_stop(lua_State *L) {
    uint8_t channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_control()->set(channel, GDMSP_CMD_STOP, NULL);
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
    gdmsp_fsm_t st  = gdmsp_control()->status(channel);
    lua_settop(L, 0);
    lua_pushstring(L, fsm_to_str(st));
    return 1;
}

static int lua_native_media_get_integer(lua_State *L) {
    uint8_t       channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_cmd_t   cmd     = (gdmsp_cmd_t)luaL_checkinteger(L, 2);
    gdmsp_value_t value   = { -1 };
    gdmsp_control()->get(channel, cmd, &value);
    lua_settop(L, 0);
    lua_pushinteger(L, (lua_Integer)value.i64);
    return 1;
}

static int lua_native_media_set_integer(lua_State *L) {
    uint8_t       channel = (uint8_t)luaL_checkinteger(L, 1);
    gdmsp_cmd_t   cmd     = (gdmsp_cmd_t)luaL_checkinteger(L, 2);
    gdmsp_value_t value   = { (int64_t)luaL_checkinteger(L, 3) };
    gdmsp_fsm_t   st      = gdmsp_control()->set(channel, cmd, &value);
    lua_settop(L, 0);
    lua_pushstring(L, fsm_to_str(st));
    return 1;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_media_bootstrap", lua_native_media_bootstrap, NULL);
    gecnd_registry("set", "lua_global_func:native_media_source", lua_native_media_source, NULL);
    gecnd_registry("set", "lua_global_func:native_media_position", lua_native_media_position, NULL);
    gecnd_registry("set", "lua_global_func:native_media_play", lua_native_media_play, NULL);
    gecnd_registry("set", "lua_global_func:native_media_pause", lua_native_media_pause, NULL);
    gecnd_registry("set", "lua_global_func:native_media_stop", lua_native_media_stop, NULL);
    gecnd_registry("set", "lua_global_func:native_media_get_status", lua_native_media_get_status, NULL);
    gecnd_registry("set", "lua_global_func:native_media_get_integer", lua_native_media_get_integer, NULL);
    gecnd_registry("set", "lua_global_func:native_media_set_integer", lua_native_media_set_integer, NULL);
}
