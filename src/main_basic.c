#include "gecnd.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

int main() {
    lua_State *L = luaL_newstate();
    gecnd_t *gly = gecnd_new(L);
    const char *const cwd = gecnd_utils_get_exe_cwd();

    luaL_openlibs(L);
    gecnd_lua_openlibs(gly);
    gecnd_set_game_file(gly, cwd, "game.lua");
    gecnd_set_engine_file(gly, cwd, "main.lua");
    gecnd_set_control_mode(gly, GECND_CONTROL_FPS | GECND_CONTROL_WINDOW);

    if (gecnd_native_callback_init(gly, 1280, 720) == GECND_OK) {
        static char *key;
        static bool pressed;
        
        while (gecnd_is_running(gly)) {
            while (gecnd_next_input(gly, &key, &pressed)) {
                gecnd_native_callback_keyboard(gly, key, pressed);
            }
            gecnd_native_callback_loop(gly, 16);
            gecnd_native_callback_draw(gly);
        }
    }

    gecnd_destroy(gly);
    return 0;
}
