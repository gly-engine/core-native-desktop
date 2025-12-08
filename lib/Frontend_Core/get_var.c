#include "gecnd.h"

uint32_t gecnd_get_flags(gecnd_t *gly) {
    return gly? gly->flags: 0;
}

uint32_t gecnd_get_sleep(gecnd_t *gly) {
    uint32_t fps = gly? (uint32_t) (100.0f/gly->target_fps) * 10.0f: 0;
    return fps > 0? fps: 0;
}
