#include "gecnd.h"
#include "gehook.h"

bool gecnd_next_input(gecnd_t *gly, char **key, bool *pressed) {
    (void) gly;
    return gly_hook_input_get_next(key, pressed);
}