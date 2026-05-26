/**
 * @author RodrigoDornelles
 * @date 26-05-26
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#include "gecnd.h"

static bool s_pressed[4][16];

static void on_key(uint32_t code, bool pressed, int port) {
    if ((unsigned)port > 3 || code >= 16) return;
    s_pressed[port][code] = pressed;
}

static void register_libretro_inputs(void) {
    gamely_daemon_input_add_keycode("libretro_internal", "a",     RETRO_DEVICE_ID_JOYPAD_A);
    gamely_daemon_input_add_keycode("libretro_internal", "b",     RETRO_DEVICE_ID_JOYPAD_B);
    gamely_daemon_input_add_keycode("libretro_internal", "c",     RETRO_DEVICE_ID_JOYPAD_X);
    gamely_daemon_input_add_keycode("libretro_internal", "d",     RETRO_DEVICE_ID_JOYPAD_Y);
    gamely_daemon_input_add_keycode("libretro_internal", "down",  RETRO_DEVICE_ID_JOYPAD_DOWN);
    gamely_daemon_input_add_keycode("libretro_internal", "e",     RETRO_DEVICE_ID_JOYPAD_SELECT);
    gamely_daemon_input_add_keycode("libretro_internal", "f",     RETRO_DEVICE_ID_JOYPAD_START);
    gamely_daemon_input_add_keycode("libretro_internal", "left",  RETRO_DEVICE_ID_JOYPAD_LEFT);
    gamely_daemon_input_add_keycode("libretro_internal", "right", RETRO_DEVICE_ID_JOYPAD_RIGHT);
    gamely_daemon_input_add_keycode("libretro_internal", "up",    RETRO_DEVICE_ID_JOYPAD_UP);

    if (!gamely_input_add_cb("libretro:libretro_internal", on_key, NULL)) {
        gamely_input_add_cb(":libretro_internal", on_key, NULL);
    }
}

int16_t RETRO_CALLCONV engine_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    static bool s_init = false;
    (void)index;
    if (!s_init) {
        register_libretro_inputs();
        s_init = true;
    }
    if (port > 3 || device != RETRO_DEVICE_JOYPAD || id >= 16) return 0;
    return s_pressed[port][id] ? 1 : 0;
}
