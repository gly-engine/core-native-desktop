#include <stdio.h>
#include <stdlib.h>
#include <ketopt.h>

#include "gecnd.h"
#include "gehook.h"

static ko_longopt_t longopts[] = {
    { "screen", ko_required_argument, 301},
    { "fps", ko_required_argument, 302 },
    { "showfps", ko_required_argument, 303 },
    { "frameskip", ko_required_argument, 304 },
    { "game", ko_required_argument, 305 },
    { "engine", ko_required_argument, 306 },
    { NULL, 0, 0 }
};

void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]) {
    if (!gly) return;

    ketopt_t opt = KETOPT_INIT;
    int c;

    while ((c = ketopt(&opt, argc, argv, 1, "s:", longopts)) >= 0) {
        if (c == 301 && sscanf(opt.arg, "%hdx%hd", &gly->width, &gly->height) != 2) {
            gly->error_string = "invalid screen size!";
        }
        if (c == 302 && (sscanf(opt.arg, "%hhi", &gly->target_fps) != 1 || gly->target_fps > 100)) {
            gly->error_string = "invalid fps!";
        }
        if (c == 304 && (sscanf(opt.arg, "%hhi", &gly->frameskip) != 1 || gly->frameskip > 10)) {
            gly->error_string = "invalid frameskip!";
        }
        if (c == 305) {
            gly->lua_game_code = opt.arg;
        }
        if (c == 306) {
            gly->lua_engine_code = opt.arg;
        }
    }
}
