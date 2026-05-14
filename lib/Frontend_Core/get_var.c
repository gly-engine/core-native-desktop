#include "gecnd.h"

uint32_t gecnd_get_flags(gecnd_t *gly) {
    return gly? gly->flags: 0;
}

uint32_t gecnd_get_sleep(gecnd_t *gly) {
    uint32_t fps = gly && gly->target_fps > 0?
        (uint32_t) (100.0f/gly->target_fps) * 10.0f: 0;
    return fps > 0? fps: 0;
}

bool gecnd_has_errors(gecnd_t *gly) {
    return gly && gly->error_len > 0;
}

const char* gecnd_get_errors(gecnd_t *gly) {
    return gly ? gly->error_buf : NULL;
}