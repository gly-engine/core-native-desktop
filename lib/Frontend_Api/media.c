#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>

#include "gehook.h"
#include "gemedia.h"
#include "gecnd.h"
#include "libretro.h"

typedef enum {
    MEDIA_MODE_NONE,
    MEDIA_MODE_VIDEO,
    MEDIA_MODE_EMULATOR
} MediaMode;

static MediaMode g_media_mode = MEDIA_MODE_NONE;

extern void libretro_init_core(void);
extern void libretro_deinit_core(void);

bool gecnd_is_emulator_running(void) {
    return g_media_mode == MEDIA_MODE_EMULATOR;
}

MediaFrame* gecnd_get_background_frame(void) {
    if (g_media_mode == MEDIA_MODE_VIDEO) {
        return avlib_get_background_frame();
    } else if (g_media_mode == MEDIA_MODE_EMULATOR) {
        return libretro_get_frame();
    }
    return NULL;
}

static int lua_native_media_bootstrap(lua_State *L) {
    lua_pushinteger(L, 1);
    return 1;
}

static int lua_native_media_stop(lua_State *L);

static int lua_native_media_source(lua_State *L) {
    uint8_t channel = luaL_checkinteger(L, 1);
    const char* url =  luaL_checkstring(L, 2);
    
    // Stop current media if on channel 0 (background)
    if (channel == 0) {
        lua_pushinteger(L, channel);
        lua_native_media_stop(L);
        lua_pop(L, 1);

        if (strncmp(url, "emu://", 6) == 0) {
            char full_path[1024];
            char cwd[1024];
            gecnd_utils_get_cwd(cwd, sizeof(cwd));
            snprintf(full_path, sizeof(full_path), "%s/%s", cwd, url + 6);
            
            libretro_init_core();
            struct retro_game_info game = {0};
            game.path = full_path;
            
            if (retro_load_game(&game)) {
                g_media_mode = MEDIA_MODE_EMULATOR;
            } else {
                fprintf(stderr, "[emu] Failed to load game: %s\n", full_path);
                libretro_deinit_core();
                g_media_mode = MEDIA_MODE_NONE;
            }
        } else {
            native_media_source(channel, url);
            g_media_mode = MEDIA_MODE_VIDEO;
        }
    } else {
        native_media_source(channel, url);
    }
    
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_position(lua_State *L) {
    uint8_t channel = luaL_checkinteger(L, 1);
    int16_t x = (int16_t) luaL_checknumber(L, 2);
    int16_t y = (int16_t) luaL_checknumber(L, 3);
    int16_t w = (int16_t) luaL_checknumber(L, 4);
    int16_t h = (int16_t) luaL_checknumber(L, 5);
    native_media_position(channel, x, y, w, h);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_play(lua_State *L) {
    uint8_t channel = luaL_checkinteger(L, 1);
    if (channel == 0 && g_media_mode == MEDIA_MODE_EMULATOR) {
        // Emulator play? For now emu is always "playing" when active
    } else {
        native_media_play(channel);
    }
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_pause(lua_State *L) {
    uint8_t channel = luaL_checkinteger(L, 1);
    if (channel == 0 && g_media_mode == MEDIA_MODE_EMULATOR) {
        // Emulator pause? 
    } else {
        native_media_pause(channel);
    }
    lua_settop(L, 0);
    return 0;
}

static int lua_native_media_stop(lua_State *L) {
    uint8_t channel = luaL_checkinteger(L, 1);
    if (channel == 0) {
        if (g_media_mode == MEDIA_MODE_EMULATOR) {
            libretro_deinit_core();
        } else if (g_media_mode == MEDIA_MODE_VIDEO) {
            native_media_stop(channel);
        }
        g_media_mode = MEDIA_MODE_NONE;
    } else {
        native_media_stop(channel);
    }
    lua_settop(L, 0);
    return 0;
}

const luaL_Reg frontend_api_media[] = {
    {"native_media_bootstrap", lua_native_media_bootstrap},
    {"native_media_source", lua_native_media_source},
    {"native_media_position", lua_native_media_position},
    {"native_media_play", lua_native_media_play},
    {"native_media_pause", lua_native_media_pause},
    {"native_media_stop", lua_native_media_stop},
    {NULL, NULL}};
