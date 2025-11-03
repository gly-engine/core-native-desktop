#include <stddef.h>

#include "gecnd.h"
#include "gehook.h"

bool gecnd_next_input(gecnd_t *gly, char **key, bool *pressed) {
    (void) gly;

    char *key_ = NULL;
    bool pressed_ = false;
    gly_hook_input_keyboard(&key_, &pressed_);

    if (!key_) return false;
    
    *key = key_;
    *pressed = pressed_;
    return true;
}