#define GLY_HOOK_IMPL
#include "gehook.h"

#include "gecnd.h"

extern bool gecnd_want_exit;
extern int gecnd_signal;

bool gecnd_is_running(gecnd_t *gly) {
    (void) gly;
    bool should_close = false;
    
    do {
        gly_hook_should_close(&should_close);
        if (should_close) break;

        should_close = gecnd_want_exit;
        if (should_close) break;

        should_close = gecnd_signal != 0;
        if (should_close) break;
    }
    while(0);


    return !should_close;
}
