/*static void lua_global_func(const char* name, void* func) {

}

__attribute__((constructor))
static void init() {
    gecnd_register_set("deamon:lua_global_func", (void*) lua_global_func);
}

static void lua_global_func(const char* name, void* func, void* usr) {
    lua_register(lua_state* usr, name, lua_function func);
}

void gecnd_new() {
    gecnd_register_get("lua_global_func:*", lua_global_func, L)
}
*/