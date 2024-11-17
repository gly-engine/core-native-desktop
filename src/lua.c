#include "zeebo.h"

static lua_State *L;

static void lua_pre_init() {
    L = luaL_newstate();

    if (!L) {
        kernel_add_error("Cannot create Lua state");
        return;
    }

    luaL_openlibs(L);
}

static void lua_init() {}

static void lua_exit() {
    if (L) {
        lua_close(L);
    }
}

lua_State *const lua() {
    return L;
}

void lua_install() {
    kernel_event_install(KERNEL_EVENT_PRE_INIT, lua_pre_init);
    kernel_event_install(KERNEL_EVENT_INIT, lua_init);
    kernel_event_install(KERNEL_EVENT_POST_EXIT, lua_exit);
}
