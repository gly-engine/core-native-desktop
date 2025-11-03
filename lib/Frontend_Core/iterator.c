#include "gecnd.h"
#include "gehook.h"

bool gecnd_next_input(gecnd_t *gly, char **key, bool *pressed) {
    (void) gly;
    bool result = false;
    gly_hook_input_keyboard(key, pressed, &result);
    return result;
}