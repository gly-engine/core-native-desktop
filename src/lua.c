#include "zeebo.h"

static lua_State *L;
static int error_handler;

static int lua_error_handler(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (msg == NULL) msg = "(error with no message)";
    luaL_traceback(L, L, msg, 1);
    return 1; 
}

static void lua_pre_init() {
    L = luaL_newstate();

    do {
        if (!L) {
            kernel_add_error("Cannot create Lua state");
            break;
        }

        lua_pushcfunction(L, lua_error_handler);
        error_handler = lua_gettop(L);

        luaL_openlibs(L);
    }
    while (0);
}

static void lua_init() {}

static void lua_exit() {
    if (L) {
        lua_close(L);
    }
}

int lua_handler(){
    return error_handler;
}

lua_State *const lua() {
    return L;
}

void lua_install() {
    kernel_event_install(KERNEL_EVENT_PRE_INIT, lua_pre_init);
    kernel_event_install(KERNEL_EVENT_INIT, lua_init);
    kernel_event_install(KERNEL_EVENT_POST_EXIT, lua_exit);
}
