#include <stdio.h>
#include <stdlib.h>
#include <ketopt.h>

#include "gecnd.h"
#include "gehook.h"

static ko_longopt_t longopts[] = {
    { "screen", ko_required_argument, 301 },
    { "fps", ko_required_argument, 302 },
    { NULL, 0, 0 }
};

void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]) {
    if (!gly) return;

    ketopt_t opt = KETOPT_INIT;
    int c;

    while ((c = ketopt(&opt, argc, argv, 1, "s:", longopts)) >= 0) {
        if (c == 301 && sscanf(opt.arg, "%hdx%hd", &gly->width, &gly->height) != 2) {
            printf("invalid screen size!\n");
            exit(1);
        }
        if (c == 302 && sscanf(opt.arg, "%hhi", &gly->target_fps) != 1) {
            printf("invalid fps!\n");
            exit(1);
        }
    }
}
