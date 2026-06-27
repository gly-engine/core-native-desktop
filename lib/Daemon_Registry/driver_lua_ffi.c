#include "gecnd.h"

static void lua_global_ffi(const char* name, void* func, void* usr) {
    /*gecnd_lang_rdsl_t ctx = {};
    lua_State *L = (lua_State*) usr;
    const char *lua_func_str = NULL;
    size_t lua_fun_len = 0;

    while(gecnd_lang_rdsl_iterator(name, ctx)) {
        if (ctx->keyidx != 1) continue;
        if (ctx->typeidx == -1) {
            lua_func_str = ctx->str;
            lua_func_len = ctx->len;
        }
        if () {
            
        }
        if (ctx->namespace == 1) {
           
        }
        if (ctx->namespace == 3) {
            // args ctx->column; ctx->type;
        }
        if (ctx->namespace == 4) {
            // rets
        }
    }*/
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "function:lua_global_ffi", lua_global_ffi, NULL);
}
