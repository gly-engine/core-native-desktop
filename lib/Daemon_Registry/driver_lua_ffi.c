#include <stdio.h>
#include "gecnd.h"

static void lua_global_ffi(const char* name, void* func, gecnd_t *const gly) {
    gecnd_lang_rdsl_t ctx = {};
    const char *lua_func_str = NULL;
    size_t lua_func_len = 0;

    while(gecnd_lang_rdsl_iterator(&ctx, name)) {
        if (ctx.keyidx != 1) continue;
        if (ctx.typeidx == -1) {
            lua_func_str = ctx.ptr;
            lua_func_len = ctx.len;
        }

    }

    if(ctx.error) {
        gecnd_add_error(gly, "[%s] %s", name, "invalid syntax!");
    }
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_ffi:testing+$u8+$u8", NULL, NULL);
    gecnd_registry("set", "function:lua_global_ffi", lua_global_ffi, NULL);
}
