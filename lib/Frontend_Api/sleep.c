#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"

static int lua_sleep_ms(lua_State *L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    lua_settop(L, 0);

    if (ms <= 0)
        return 0;

#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR);
#endif

    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:sleep_ms", lua_sleep_ms, NULL);
}
