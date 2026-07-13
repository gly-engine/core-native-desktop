/**
 * @author RodrigoDornelles
 * @date 26-05-26
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#include "main.h"

static bool s_pressed[4][16];

static void on_key(uint32_t code, bool pressed, int port) {
    if ((unsigned)port > 3 || code >= 16) return;
    s_pressed[port][code] = pressed;
}

/* Injeção direta de botão (ex.: controle virtual via Lua/FFI).
 * keyidx = RETRO_DEVICE_ID_JOYPAD_* (0..15). */
void native_libretro_keyboard(uint8_t port, uint8_t keyidx, bool press) {
    on_key(keyidx, press, port);
}

static struct {
    typeof(gamely_daemon_input_add_keycode) *add_keycode;
    typeof(gamely_input_add_cb)             *add_cb;
} input;

static bool input_bind(void) {
    if (input.add_keycode) return true;
    api->registry("get", "function:gamely_daemon_input_add_keycode", (void *)&input.add_keycode, NULL);
    api->registry("get", "function:gamely_input_add_cb",             (void *)&input.add_cb,      NULL);
    return input.add_keycode != NULL;
}

static void register_libretro_inputs(void) {
    if (!input_bind()) return;
    input.add_keycode("libretro_internal", "a",     RETRO_DEVICE_ID_JOYPAD_A);
    input.add_keycode("libretro_internal", "b",     RETRO_DEVICE_ID_JOYPAD_B);
    input.add_keycode("libretro_internal", "c",     RETRO_DEVICE_ID_JOYPAD_X);
    input.add_keycode("libretro_internal", "d",     RETRO_DEVICE_ID_JOYPAD_Y);
    input.add_keycode("libretro_internal", "down",  RETRO_DEVICE_ID_JOYPAD_DOWN);
    input.add_keycode("libretro_internal", "e",     RETRO_DEVICE_ID_JOYPAD_SELECT);
    input.add_keycode("libretro_internal", "f",     RETRO_DEVICE_ID_JOYPAD_START);
    input.add_keycode("libretro_internal", "left",  RETRO_DEVICE_ID_JOYPAD_LEFT);
    input.add_keycode("libretro_internal", "right", RETRO_DEVICE_ID_JOYPAD_RIGHT);
    input.add_keycode("libretro_internal", "up",    RETRO_DEVICE_ID_JOYPAD_UP);

    if (!input.add_cb("libretro:libretro_internal", on_key, NULL)) {
        input.add_cb(":libretro_internal", on_key, NULL);
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
