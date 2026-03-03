#include <stdbool.h>
#include <string.h>
#include "gecnd.h"

static bool key_states[GECND_KEY_COUNT] = {0};

void gecnd_key_set_state(gecnd_key_t key, bool pressed) {
    if (key > GECND_KEY_NULL && key < GECND_KEY_COUNT) {
        key_states[key] = pressed;
    }
}

bool gecnd_key_get_state(gecnd_key_t key) {
    if (key > GECND_KEY_NULL && key < GECND_KEY_COUNT) {
        return key_states[key];
    }
    return false;
}
