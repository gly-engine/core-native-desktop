#include <raylib.h>

#include "gehook.h"

static const struct {
    int key;
    const char *name;
} keymap[] = {
    { KEY_UP,          "up" },
    { KEY_DOWN,        "down" },
    { KEY_LEFT,        "left" },
    { KEY_RIGHT,       "right" },
    { KEY_Z,           "a" },
    { KEY_X,           "b" },
    { KEY_C,           "c" },
    { KEY_V,           "d" },
    { KEY_LEFT_SHIFT,  "menu" }
//    { KEY_H,           "ch_down" },
//    { KEY_L,           "vol_up" },
//    { KEY_COMMA,           "ch_up" },
//    { KEY_K,           "vol_down" }
};

#define KEYMAP_COUNT (sizeof(keymap) / sizeof(keymap[0]))

static bool old_state[KEYMAP_COUNT];

void gly_hook_keyboard_has_media(bool *enable)
{
    *enable = true;
}

void gly_hook_input_keyboard(uint8_t index, char** key, bool* press)
{
    if (index >= KEYMAP_COUNT) {
        return;
    }

    bool new_state = IsKeyDown(keymap[index].key);

    if (new_state != old_state[index]) {
        old_state[index] = new_state;
        *key = (char*) keymap[index].name;
        *press = new_state;
        return;
    }

    *press = true;
}
