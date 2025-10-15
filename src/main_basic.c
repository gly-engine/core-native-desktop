#include "gecnd.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

int main() {
    gecnd_t gly = gecnd_new();
    lua_State *L = luaL_newstate();
    const char *const cwd = gecnd_utils_get_exe_cwd();

    luaL_openlibs(L);
    gecnd_lua_open_graphics(gly, L);
    gecnd_set_game_file(gly, cwd, "game.lua");
    gecnd_set_engine_file(gly, cwd, "engine.lua");
    gecnd_set_control_mode(gly, GECND_CONTROL_FPS | GECND_CONTROL_WINDOW);

    if (gecnd_native_callback_init(gly, 1280, 720) != 0) {
        return 1;
    }

    for (;;) {
        gecnd_native_callback_loop(gly, 16);
        gecnd_native_callback_draw(gly);
    }

    gecnd_destroy(gly);
    return 0;
}
