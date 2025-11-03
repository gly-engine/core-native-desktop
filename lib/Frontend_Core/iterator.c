#include "gecnd.h"
#include "gehook.h"

bool gecnd_next_input(gecnd_t *gly, char **key, bool *pressed) {
    (void) gly;
    gly_hook_input_get_next(key, pressed);
    return false;
}