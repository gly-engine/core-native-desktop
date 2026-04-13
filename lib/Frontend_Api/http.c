#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"
#include "gamely_webclient.h"

static void cb_push(lua_State *L, int64_t req_id, const char *evt)
{
    int fn_ref = 0;
    if (!fn_ref) {
        lua_getglobal(L, "native_callback_http");
        fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, fn_ref);
    lua_pushinteger(L, req_id);
    lua_pushstring(L, evt);
}

static const char *cb_get_str(lua_State *L, int64_t req_id, const char *evt)
{
    cb_push(L, req_id, evt);
    if (lua_pcall(L, 2, 1, 0)) { lua_pop(L, 1); return NULL; }
    const char *s = luaL_checkstring(L, -1);
    lua_pop(L, 1);
    return s;
}

static void cb_promise(lua_State *L, int64_t req_id)
{
    cb_push(L, req_id, "async-promise");
    lua_pcall(L, 2, 0, 0);
}

static void cb_resolve(lua_State *L, int64_t req_id)
{
    cb_push(L, req_id, "async-resolve");
    lua_pcall(L, 2, 0, 0);
}

static void cb_error_immediate(lua_State *L, const char *msg)
{
    lua_getfield(L, 1, "set"); lua_pushstring(L, "body");  lua_pushstring(L, "");   lua_pcall(L, 2, 0, 0);
    lua_getfield(L, 1, "set"); lua_pushstring(L, "ok");    lua_pushboolean(L, 0);   lua_pcall(L, 2, 0, 0);
    lua_getfield(L, 1, "set"); lua_pushstring(L, "error"); lua_pushstring(L, msg);  lua_pcall(L, 2, 0, 0);
}

typedef struct {
    lua_State *L;
    int64_t    req_id;
} req_ctx_t;

static void on_status(gly_wc_id_t id, int status, void *user)
{
    req_ctx_t *ctx = user;
    cb_push(ctx->L, ctx->req_id, "set-status");
    lua_pushinteger(ctx->L, status);
    lua_pcall(ctx->L, 3, 0, 0);
}

static void on_data(gly_wc_id_t id, const char *data, size_t len, void *user)
{
    req_ctx_t *ctx = user;
    cb_push(ctx->L, ctx->req_id, "add-body-data");
    lua_pushlstring(ctx->L, data, len);
    lua_pcall(ctx->L, 3, 0, 0);
}

static void on_done(gly_wc_id_t id, void *user)
{
    req_ctx_t *ctx = user;
    cb_resolve(ctx->L, ctx->req_id);
    free(ctx);
}

static void on_error(gly_wc_id_t id, const char *msg, void *user)
{
    req_ctx_t *ctx = user;
    cb_push(ctx->L, ctx->req_id, "set-error");
    lua_pushstring(ctx->L, msg);
    lua_pcall(ctx->L, 3, 0, 0);
    free(ctx);
}

static int64_t g_auto_id = -1;
static int     g_started = 0;

static int lua_native_http_handler(lua_State *L)
{
    if (!g_started) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);
        gecnd_t *gly = lua_touserdata(L, -1);
        lua_pop(L, 1);
        if (!gly || !gly->loop) {
            cb_error_immediate(L, "[core:error] libuv is not started!");
            return 0;
        }
        gamely_daemon_webclient_start(gly->loop);
        g_started = 1;
    }

    int64_t req_id = luaL_checkinteger(L, 2);
    if (req_id == 0) req_id = g_auto_id--;

    const char *url    = cb_get_str(L, req_id, "get-fullurl");
    const char *method = cb_get_str(L, req_id, "get-method");
    const char *body   = cb_get_str(L, req_id, "get-body-data");

    req_ctx_t *ctx = malloc(sizeof(req_ctx_t));
    if (!ctx) { cb_error_immediate(L, "out of memory"); return 0; }
    ctx->L      = L;
    ctx->req_id = req_id;
    fprintf(stderr, "new request %li\n", req_id);

    gly_wc_id_t wc_id = gamely_daemon_webclient_http(
        method, url, body,
        on_status, on_data, on_done, on_error,
        ctx
    );

    if (!wc_id) {
        free(ctx);
        cb_error_immediate(L, "failed to connect");
        return 0;
    }

    cb_promise(L, req_id);
    return 0;
}

void gly_hook_luaopen_http(lua_State *L)
{
    lua_pushcfunction(L, lua_native_http_handler);
    lua_setglobal(L, "native_http_handler");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_ssl");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_callback");
}

void gly_hook_luaclose_http(lua_State *L)
{
    (void)L;
    if (g_started) {
        gamely_daemon_webclient_stop();
        g_started = 0;
    }
}
