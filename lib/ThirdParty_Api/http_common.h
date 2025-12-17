

void native_callback_http(lua_State* L, int64_t req_id, const char *const evt_key)
{
    static int native_http_callback;
    if (!native_http_callback) {
        lua_getglobal(L, "native_callback_http");
        native_http_callback = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, native_http_callback);
    lua_pushinteger(L, req_id);
    lua_pushstring(L, evt_key);
}

const char* native_http_get_str(lua_State* L, int64_t req_id, const char *const evt_key)
{
    native_callback_http(L, req_id, evt_key);
    if (lua_pcall(L, 2, 1, 0)) {
        lua_pop(L, 1);
        return NULL;
    }
    const char *s = luaL_checkstring(L, -1);
    lua_pop(L, 1);
    return s;
}

void native_http_promise(lua_State* L, int64_t req_id) {
    native_callback_http(L, req_id, "async-promise");
    lua_pcall(L, 2, 0, 0);
}

void native_http_resolve(lua_State* L, int64_t req_id) {
    native_callback_http(L, req_id, "async-resolve");
    lua_pcall(L, 2, 0, 0);
}

void native_http_immediate_error(lua_State* L, char* message) {
    lua_getfield(L, 1, "set");
    lua_pushstring(L, "body");
    lua_pushstring(L, "");
    lua_pcall(L, 2, 0, 0);

    lua_getfield(L, 1, "set");
    lua_pushstring(L, "ok");
    lua_pushboolean(L, 0);
    lua_pcall(L, 2, 0, 0);

    lua_getfield(L, 1, "set");
    lua_pushstring(L, "error");
    lua_pushstring(L, message);
    lua_pcall(L, 2, 0, 0);
}
