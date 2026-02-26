#include <lauxlib.h>
#include <lua.h>
#include <string.h>

#include "gecnd.h"
#include "libretro.h"

static bool joypad_state[16] = {0};

void engine_set_input_button(int id, bool pressed) {
    if (id >= 0 && id < 16) {
        joypad_state[id] = pressed;
    }
}

int16_t RETRO_CALLCONV engine_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    
    if (id < 16) {
        return joypad_state[id] ? 1 : 0;
    }
    
    return 0;
}

// Map engine keys (strings from Lua) to libretro joypad IDs
static void engine_input_map_key(const char* key, bool pressed) {
    if (strcmp(key, "up") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_UP, pressed);
    else if (strcmp(key, "down") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_DOWN, pressed);
    else if (strcmp(key, "left") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_LEFT, pressed);
    else if (strcmp(key, "right") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_RIGHT, pressed);
    else if (strcmp(key, "a") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_A, pressed);
    else if (strcmp(key, "b") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_B, pressed);
    else if (strcmp(key, "select") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_SELECT, pressed);
    else if (strcmp(key, "start") == 0) engine_set_input_button(RETRO_DEVICE_ID_JOYPAD_START, pressed);
}

void gecnd_set_btn_state(gecnd_t *gly, const char* key, bool pressed) {
    if (gly && key) {
        engine_input_map_key(key, pressed);
        lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
        lua_pushstring(gly->L, key);
        lua_pushboolean(gly->L, pressed);
        if (lua_pcall(gly->L, 2, 0, 0)) {
            gly->error_string = luaL_checkstring(gly->L, -1);
        }
    }
}
