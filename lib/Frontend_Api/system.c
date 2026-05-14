#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include <lua.h>
#include <lauxlib.h>

#include "gecnd.h"

static int lua_native_system_exit(lua_State *L) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);
    gecnd_t *gly = lua_touserdata(L, -1);
    lua_settop(L, 0);
    gly->state = GECND_FSM_EXITING;
    return 0;
}

static int lua_native_system_get_language(lua_State *L) {
    const char *raw = NULL;
    static char lang[8] = "en-US";

#ifdef _WIN32
    static wchar_t wbuf[LOCALE_NAME_MAX_LENGTH];
    static char utf8[16];
    if (GetUserDefaultLocaleName(wbuf, LOCALE_NAME_MAX_LENGTH) > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8, sizeof(utf8), NULL, NULL);
        raw = utf8;
    }
#else
    raw = getenv("LANG");
    if (!raw)
        raw = setlocale(LC_ALL, NULL);
#endif

    if (raw && strlen(raw) >= 4) {
        lang[0] = tolower(raw[0]);
        lang[1] = tolower(raw[1]);
        lang[2] = '-';
        lang[3] = toupper(raw[3]);
        lang[4] = toupper(raw[4]);
        lang[5] = '\0';
    }

    lua_pushstring(L, lang);
    return 1;
}

static int lua_native_system_get_env(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *val = getenv(name);

#ifdef _WIN32
    if (!val) {
        static char buffer[512];
        DWORD len = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
        if (len > 0 && len < sizeof(buffer))
            val = buffer;
    }
#endif

    if (val)
        lua_pushstring(L, val);
    else
        lua_pushnil(L);

    return 1;
}


const luaL_Reg frontend_api_system[] = {
    {"native_system_get_language", lua_native_system_get_language},
    {"native_system_get_env", lua_native_system_get_env},
    {"native_system_exit", lua_native_system_exit},
    {NULL, NULL}
};
