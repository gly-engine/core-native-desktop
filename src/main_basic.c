#include <signal.h>

#include "gecnd.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

int main(int argc, char* argv[]) {
    lua_State *L = luaL_newstate();
    gecnd_t *gly = gecnd_new2(L, argc, argv);

    luaL_openlibs(L);
    signal(SIGINT, gecnd_handler);

    if (gly && gecnd_native_callback_init(gly, gly->width, gly->height) == GECND_OK) {
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
