#include <string.h>
#include <stdlib.h>
#include <lua.h>
#include "gecnd.h"

gecnd_t *gecnd_new(lua_State* L) {
    gecnd_t *gly = NULL;
    do {
        if (!L) {
            break;
        }

        gly = (gecnd_t*) malloc(sizeof(gecnd_t));

        if (!gly) {
            break;
        }

        gly->L = L;
    }
    while(0);
    return gly;   
}

void gecnd_destory(gecnd_t *gly) {
    if (gly) {
        free(gly);
    }
}
