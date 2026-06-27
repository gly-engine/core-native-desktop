static void lua_global_func(const char* name, void* func) {

}

__attribute__((constructor))
static void init() {
    //gecnd_register("deamon:lua_global_func", (void*) lua_global_func);
}
