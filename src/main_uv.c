#include <lauxlib.h>
#include <lualib.h>
#include <lua.h>
#include <uv.h>

#include "gecnd.h"

void main_rc(uv_loop_t* loop);

static void cb_frame(uv_timer_t *t) {
    static uint64_t last = 0;
    uint64_t now = uv_hrtime();
    uint64_t delta_ms = (last == 0) ? 16: (now - last) / 1000000;  

    gecnd_t *gly = t->data;
    last = now;

    gecnd_set_delta(gly, delta_ms);
    if (!gecnd_update(gly)) {
        uv_stop(t->loop);
    }
}

int main(int argc, char* argv[]) {
    static uv_loop_t loop;
    static uv_timer_t timer;
    lua_State *L = luaL_newstate();
    gecnd_t *gly = gecnd_new(L);
    int exit_code = 0;
    
    luaL_openlibs(L);
    uv_loop_init(&loop);
    uv_timer_init(&loop, &timer);
    gecnd_set_loop(gly, (void*) &loop);
    gecnd_set_args(gly, argc, argv);

    /** @todo move */
    main_rc(&loop);

    timer.data = gly;
    uv_timer_start(&timer, cb_frame, 0, gecnd_get_sleep(gly));
    uv_run(&loop, UV_RUN_DEFAULT);

    if (gecnd_has_errors(gly)) {
        const char* message = gecnd_get_errors(gly);
        printf("%s", message);
        exit_code = 1;
    }
    
    uv_loop_close(&loop);
    gecnd_destroy(gly);
    lua_close(L);

    return exit_code;
}
