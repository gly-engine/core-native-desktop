#include "gecnd.h"
#include "gehook.h"

void gecnd_set_loop(gecnd_t *gly, void* loop) {
    if (gly && loop) {
        if (!gly->loop) {
            gly_hook_disable_delay();
        }
        gly->loop = loop;
    }
}
