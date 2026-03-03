#include <stdint.h>
#include "libretro.h"
#include "gecnd.h"

int16_t RETRO_CALLCONV engine_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;

    gecnd_key_t key = GECND_KEY_NULL;
    switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_B:      key = GECND_KEY_B; break;
        case RETRO_DEVICE_ID_JOYPAD_A:      key = GECND_KEY_A; break;
        case RETRO_DEVICE_ID_JOYPAD_X:      key = GECND_KEY_C; break;
        case RETRO_DEVICE_ID_JOYPAD_Y:      key = GECND_KEY_D; break;
        case RETRO_DEVICE_ID_JOYPAD_UP:     key = GECND_KEY_UP; break;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:   key = GECND_KEY_DOWN; break;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:   key = GECND_KEY_LEFT; break;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:  key = GECND_KEY_RIGHT; break;
        case RETRO_DEVICE_ID_JOYPAD_SELECT: key = GECND_KEY_E; break;
        case RETRO_DEVICE_ID_JOYPAD_START:  key = GECND_KEY_F; break;
        default: break;
    }

    return gecnd_key_get_state(key) ? 1 : 0;
}
