#include <stddef.h>

#include "gecnd.h"
#include "gehook.h"

bool gecnd_next_input(gecnd_t *gly, char **key, bool *pressed)
{
    char *key_ = NULL;
    bool pressed_ = false;
    gly_hook_input_keyboard(gly->key_index, &key_, &pressed_);


    if (!key_ && !pressed_) return false;
    gly->key_index++;

    if (!key_) return true;
    
    *key = key_;
    *pressed = pressed_;
    return true;
}