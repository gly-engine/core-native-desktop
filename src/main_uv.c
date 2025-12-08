#include <signal.h>

#include <lauxlib.h>
#include <lualib.h>
#include <lua.h>
#include <uv.h>

#include "gecnd.h"

static void frame(uv_timer_t *t) {
    static char *key;
    static bool pressed;
    
    gecnd_t *gly = t->data;

    while (gecnd_next_input(gly, &key, &pressed)) {
        gecnd_native_callback_keyboard(gly, key, pressed);
    }

    gecnd_native_callback_loop(gly, 16);
    gecnd_native_callback_draw(gly);

    if (!gecnd_is_running(gly)) {
        uv_stop(t->loop);
    }
}

int main(int argc, char* argv[]) {
    static uv_loop_t loop;
    static uv_timer_t timer;
    lua_State *L = luaL_newstate();
    gecnd_t *gly = gecnd_new2(L, argc, argv);
    
    luaL_openlibs(L);
    uv_loop_init(&loop);
    gecnd_set_loop(gly, (void*) &loop);
    signal(SIGINT, gecnd_handler);

    if (gly && gecnd_native_callback_init(gly, gly->width, gly->height) == GECND_OK) {
        uv_timer_init(&loop, &timer);
        timer.data = gly;

        uv_timer_start(&timer, frame, 0, 16);
        uv_run(&loop, UV_RUN_DEFAULT);
    }

    uv_loop_close(&loop);
    gecnd_destroy(gly);
    lua_close(L);

    return 0;
}
