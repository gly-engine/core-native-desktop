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

void gecnd_set_screensize(gecnd_t *gly, int16_t width, int16_t height)
{
    if (gly && width > 0 && height > 0) {
        gly->width = width;
        gly->height = height;
    }
}
