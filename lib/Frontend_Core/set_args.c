#include <stdio.h>
#include <stdlib.h>
#include <ketopt.h>

#include "gecnd.h"
#include "gehook.h"

static ko_longopt_t longopts[] = {
    { "screen", ko_required_argument, 's' },
    { "fps", ko_required_argument, 'f' },
    { "frameskip", ko_required_argument, 'k' },
    { NULL, 0, 0 }
};

void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]) {
    if (!gly) return;

    ketopt_t opt = KETOPT_INIT;
    int c;

    while ((c = ketopt(&opt, argc, argv, 1, "s:", longopts)) >= 0) {
        if (c == 's' && sscanf(opt.arg, "%hdx%hd", &gly->width, &gly->height) != 2) {
            gly->error_string = "invalid screen size!";
        }
        if (c == 'f' && (sscanf(opt.arg, "%hhi", &gly->target_fps) != 1 || gly->target_fps > 100)) {
            gly->error_string = "invalid fps!";
        }
        if (c == 'k' && (sscanf(opt.arg, "%hhi", &gly->frameskip) != 1 || gly->frameskip > 10)) {
            gly->error_string = "invalid frameskip!";
        }
    }
}
