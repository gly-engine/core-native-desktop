#include <signal.h>

#include "gecnd.h"
#include "lua.h"
#include "lualib.h"

int main(int argc, char* argv[]) {
    lua_State *L = luaL_newstate();
    gecnd_t *gly = gecnd_new(L);
    int exit_code = 0;

    luaL_openlibs(L);
    gecnd_set_args(gly, argc, argv);

    while(gecnd_update(gly)) {
        continue;
    }

    if (gecnd_has_errors(gly)) {
        const char* message = gecnd_get_errors(gly);
        printf("%s", message);
        exit_code = 1;
    }

    gecnd_destroy(gly);
    lua_close(L);
    return exit_code;
}
