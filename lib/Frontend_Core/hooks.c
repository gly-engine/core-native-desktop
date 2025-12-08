#define GLY_HOOK_IMPL
#include "gehook.h"

#include "gecnd.h"

extern int gecnd_signal;

bool gecnd_is_running(gecnd_t *gly) {
    bool should_close = false;
    
    do {
        gly_hook_should_close(&should_close);
        if (should_close) break;

        should_close = gly->want_exit;
        if (should_close) break;

        should_close = gecnd_signal != 0;
        if (should_close) break;

        should_close = gly->error_code != 0;
        if (should_close) break;
    }
    while(0);


    return !should_close;
}
