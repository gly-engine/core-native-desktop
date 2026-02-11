#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"

void gecnd_set_btn_state(gecnd_t *gly, const char* key, bool pressed) {
    if (gly && key) {
        lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_keyboard);
        lua_pushstring(gly->L, key);
        lua_pushboolean(gly->L, pressed);
        if (lua_pcall(gly->L, 2, 0, 0)) {
            gly->error_string = luaL_checkstring(gly->L, -1);
        }
    }
}
