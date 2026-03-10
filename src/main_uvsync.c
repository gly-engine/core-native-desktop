#include <lauxlib.h>
#include <lualib.h>
#include <lua.h>
#include <uv.h>

#include "gecnd.h"

void main_rc(uv_loop_t* loop);

int main(int argc, char* argv[]) {
    static uv_loop_t loop;
    lua_State *L = luaL_newstate();
    gecnd_t *gly = gecnd_new(L);
    int exit_code = 0;
    
    luaL_openlibs(L);
    uv_loop_init(&loop);
    gecnd_set_loop(gly, (void*) &loop);
    gecnd_set_args(gly, argc, argv);

    /** @todo move */
    //main_rc(&loop);

    do {
        uv_run(&loop, UV_RUN_NOWAIT);
        gecnd_set_delta(gly, gecnd_get_delta_ms());
    }
    while(gecnd_update(gly));

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
