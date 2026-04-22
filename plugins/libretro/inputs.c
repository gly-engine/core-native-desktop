#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"


#define LR_KEY_COUNT 10

typedef struct { const char *name; unsigned id; } lr_entry_t;

/* sorted by name for bsearch in on_key */
static const lr_entry_t s_keymap[LR_KEY_COUNT] = {
    { "a",     RETRO_DEVICE_ID_JOYPAD_A      },
    { "b",     RETRO_DEVICE_ID_JOYPAD_B      },
    { "c",     RETRO_DEVICE_ID_JOYPAD_X      },
    { "d",     RETRO_DEVICE_ID_JOYPAD_Y      },
    { "down",  RETRO_DEVICE_ID_JOYPAD_DOWN   },
    { "e",     RETRO_DEVICE_ID_JOYPAD_SELECT },
    { "f",     RETRO_DEVICE_ID_JOYPAD_START  },
    { "left",  RETRO_DEVICE_ID_JOYPAD_LEFT   },
    { "right", RETRO_DEVICE_ID_JOYPAD_RIGHT  },
    { "up",    RETRO_DEVICE_ID_JOYPAD_UP     },
};

/* indexed directly by RETRO_DEVICE_ID_JOYPAD_* (0-15) */
static bool s_pressed[4][16];

static int cmp_name(const void *key, const void *elem) {
    return strcmp((const char *)key, ((const lr_entry_t *)elem)->name);
}

static void on_key(const char *name, bool pressed, int port, void *usr) {
    (void)usr;
    if ((unsigned)port > 3) return;
    const lr_entry_t *e = bsearch(name, s_keymap, LR_KEY_COUNT, sizeof(s_keymap[0]), cmp_name);
    if (!e) return;
    s_pressed[port][e->id] = pressed;
}

int16_t RETRO_CALLCONV engine_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    static bool s_init = false;
    (void)index;
    if (!s_init) {
        gamely_daemon_input_subscribe(on_key, NULL);
        s_init = true;
    }
    if (port > 3 || device != RETRO_DEVICE_JOYPAD || id >= 16) return 0;
    return s_pressed[port][id] ? 1 : 0;
}
