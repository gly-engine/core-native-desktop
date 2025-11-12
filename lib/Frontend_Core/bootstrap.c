#include <stdio.h>
#include <stdlib.h>
#include <ketopt.h>

#include "gecnd.h"
#include "gehook.h"

static ko_longopt_t longopts[] = {
    { "screen", ko_required_argument, 301 },
    { NULL, 0, 0 }
};

gecnd_t *gecnd_new2(lua_State* L, int argc, char* argv[]) {
    gecnd_t *gly = gecnd_new(L);
    ketopt_t opt = KETOPT_INIT;
    int c;

    do {
        if (!gly) {
            break;
        }

        while ((c = ketopt(&opt, argc, argv, 1, "s:", longopts)) >= 0) {
            if (c == 301 && sscanf(opt.arg, "%hdx%hd", &gly->width, &gly->height) != 2) {
                printf("invalid screen size!\n");
                exit(1);
            }
        }

    }
    while(0);

    return gly;
}
