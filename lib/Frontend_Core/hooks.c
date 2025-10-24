#define GLY_HOOK_IMPL
#include "gehook.h"

#include "gecnd.h"

bool gecnd_is_running(gecnd_t *gly) {
    (void) gly;
    bool should_close = false;
    gly_hook_should_close(&should_close);
    return !should_close;
}
