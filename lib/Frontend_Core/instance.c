#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gehook.h"

#ifndef GECND_DEFAULT_WIDTH
#define GECND_DEFAULT_WIDTH 1280
#endif

#ifndef GECND_DEFAULT_HEIGHT
#define GECND_DEFAULT_HEIGHT 720
#endif

#ifndef GECND_DEFAULT_FPS
#define GECND_DEFAULT_FPS 60
#endif

#ifndef GECND_DEFAULT_FRAMESKIP
#define GECND_DEFAULT_FRAMESKIP 0
#endif

static gecnd_t *instance;

void gecnd_global_signal_init();
void gly_hook_luaopen_http(lua_State *L);

gecnd_t *gecnd_new(lua_State* L) {
    gecnd_t *gly = NULL;
    bool native_keyboard_has_media = false;

    gecnd_global_signal_init();

    do {
        if (!L) {
            break;
        }

        gly = (gecnd_t*) malloc(sizeof(gecnd_t));

        if (!gly) {
            break;
        }

        (void) memset(gly, 0, sizeof(gecnd_t));

        lua_pushlightuserdata(L, gly);
        lua_rawseti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);

        gly->L = L;
        gly->state = GECND_FSM_BOOT;
        gly->width = GECND_DEFAULT_WIDTH;
        gly->height = GECND_DEFAULT_HEIGHT;
        gly->target_fps = GECND_DEFAULT_FPS;
        gly->frameskip = GECND_DEFAULT_FRAMESKIP;
        gly->flags = GECND_FLAG_TIMER_PREFER_BACKEND;
    }
    while(0);

    if(!instance) {
        instance = gly;
        gecnd_filter_reset_effects();
    }

    return gly;   
}

void gecnd_destroy(gecnd_t *gly) {
    if (gly) {
        if (gecnd_is_root(gly))
            gamely_hypervisor_exit();
        free(gly->engine_source.fetch.buf);
        free(gly->game_source.fetch.buf);
        free(gly->error_buf);
        gly_hook_luaclose_storage(gly->L);
        free(gly);
    }
}

void gecnd_set_state(gecnd_t *gly, gecnd_fsm_t new_state) {
    if (!gly) return;
    bool cur_ok = gly->state >= GECND_FSM_RUNNING && gly->state <= GECND_FSM_RUNNING_STANDBY;
    bool new_ok = new_state >= GECND_FSM_RUNNING && new_state <= GECND_FSM_RUNNING_STANDBY;
    if (cur_ok && new_ok)
        gly->state = new_state;
}

bool gecnd_is_root(gecnd_t *gly) {
    return gly != NULL && gly == instance;    
}

gecnd_t *gecnd_get_root() {
    return instance;
}

gecnd_fsm_t gecnd_get_state(gecnd_t *gly) {
    return gly ? gly->state : GECND_FSM_EXITING_FORCE;
}
