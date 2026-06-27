#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <lauxlib.h>
#include <lua.h>

#include "gecnd.h"

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
    const char *s = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
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
    lua_State    *L;
    int64_t       req_id;
    gly_req_id_t  wc_id;
    int           is_ws;
    int           lua_close;
} req_ctx_t;

/**
 * @todo move to daemon WebLoop ownning pedings and *usr from frontend.
 */
#define MAX_PENDING 64
static req_ctx_t *g_pending[MAX_PENDING];

static void pending_add(req_ctx_t *ctx)
{
    for (int i = 0; i < MAX_PENDING; i++)
        if (!g_pending[i]) { g_pending[i] = ctx; return; }
}

static req_ctx_t *pending_find(int64_t req_id)
{
    for (int i = 0; i < MAX_PENDING; i++)
        if (g_pending[i] && g_pending[i]->req_id == req_id) return g_pending[i];
    return NULL;
}

/* Returns 1 if ctx was still pending (and is now removed), 0 if it had already
 * been claimed. Lets each terminal path (on_done / on_error / immediate connect
 * failure) own the free exactly once, even if the webclient driver both invokes
 * on_error AND returns 0 for the same synchronous failure. */
static int pending_remove(req_ctx_t *ctx)
{
    for (int i = 0; i < MAX_PENDING; i++)
        if (g_pending[i] == ctx) { g_pending[i] = NULL; return 1; }
    return 0;
}

static void on_status(gly_req_id_t id, int status, void *user)
{
    req_ctx_t *ctx = user;
    cb_push(ctx->L, ctx->req_id, "set-status");
    lua_pushinteger(ctx->L, status);
    lua_pcall(ctx->L, 3, 0, 0);
}

static void on_data(gly_req_id_t id, const char *data, size_t len, void *user)
{
    req_ctx_t *ctx = user;
    cb_push(ctx->L, ctx->req_id, "add-body-data");
    lua_pushlstring(ctx->L, data, len);
    lua_pcall(ctx->L, 3, 0, 0);
}

static void on_done(gly_req_id_t id, void *user)
{
    req_ctx_t *ctx = user;
    if (!pending_remove(ctx)) return;   /* already finished by another path */
    cb_resolve(ctx->L, ctx->req_id);
    free(ctx);
}

static void on_error(gly_req_id_t id, const char *msg, void *user)
{
    req_ctx_t *ctx = user;
    if (!pending_remove(ctx)) return;   /* already finished by another path */
    cb_push(ctx->L, ctx->req_id, "set-error");
    lua_pushstring(ctx->L, msg);
    lua_pcall(ctx->L, 3, 0, 0);
    cb_resolve(ctx->L, ctx->req_id);
    free(ctx);
}

static void on_ws_open(gly_req_id_t id, void *user)
{
    req_ctx_t *ctx = user;
    cb_push(ctx->L, ctx->req_id, "set-status");
    lua_pushinteger(ctx->L, 200);
    lua_pcall(ctx->L, 3, 0, 0);
    cb_resolve(ctx->L, ctx->req_id);
}

static void on_ws_msg(gly_req_id_t id, const char *data, size_t len, void *user)
{
    req_ctx_t *ctx = user;
    cb_push(ctx->L, ctx->req_id, "sock-message");
    lua_pushlstring(ctx->L, data, len);
    lua_pcall(ctx->L, 3, 0, 0);
}

static void on_ws_close(gly_req_id_t id, void *user)
{
    req_ctx_t *ctx = user;
    if (!pending_remove(ctx)) return;   /* already finished by another path */
    if (!ctx->lua_close) {
        cb_push(ctx->L, ctx->req_id, "sock-event");
        lua_pushstring(ctx->L, "disconnect");
        lua_pcall(ctx->L, 3, 0, 0);
    }
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
    int         is_ws  = method && strcmp(method, "SOCK") == 0;
    const char *body    = NULL;
    const char *upgrade = NULL;

    if (is_ws) {
        upgrade = cb_get_str(L, req_id, "get-sock-upgrade");
    } else {
        body = cb_get_str(L, req_id, "get-body");
    }

    /**
     * @todo iterate request headers via get-header-count / get-header-name /
     *       get-header-data and forward them to the driver. Will require
     *       extending gly_http_req_t with header_names/header_values/header_count
     *       (shared between HTTP and WS) and adding header injection in
     *       LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER inside driver_warmcat.c.
     */

    if (!url) { cb_error_immediate(L, "missing url"); return 0; }

    req_ctx_t *ctx = malloc(sizeof(req_ctx_t));
    if (!ctx) { cb_error_immediate(L, "out of memory"); return 0; }
    lua_rawgeti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);
    gecnd_t *gly   = lua_touserdata(L, -1);
    lua_pop(L, 1);
    ctx->L         = gly->L;
    ctx->req_id    = req_id;
    ctx->wc_id     = 0;
    ctx->is_ws     = is_ws;
    ctx->lua_close = 0;
    pending_add(ctx);
    fprintf(stderr, "new request %li\n", req_id);

    gly_req_id_t wc_id;
    if (is_ws) {
        wc_id = gamely_daemon_webclient_ws_connect(
            url, upgrade,
            on_ws_open, on_ws_msg, on_ws_close, on_error,
            ctx
        );
    } else {
        gly_http_req_t req = { .method = method, .body = body, .body_len = body ? strlen(body) : 0 };
        wc_id = gamely_daemon_webclient_http(
            url, &req,
            on_status, on_data, on_done, on_error,
            ctx
        );
    }

    if (!wc_id) {
        /* If the driver already reported the failure via on_error it freed ctx
         * and removed it from pending; only clean up here when it didn't. */
        if (pending_remove(ctx)) {
            free(ctx);
            cb_error_immediate(L, "failed to connect");
        }
        return 0;
    }
    ctx->wc_id = wc_id;

    cb_promise(L, req_id);
    return 0;
}

static int lua_native_http_sock(lua_State *L)
{
    int64_t    req_id = luaL_checkinteger(L, 1);
    int        op     = (int)luaL_checkinteger(L, 2);
    req_ctx_t *ctx    = pending_find(req_id);

    switch (op) {
    case 1: {
        size_t      len  = 0;
        const char *data = luaL_checklstring(L, 3, &len);
        if (!ctx || !ctx->is_ws) { lua_pushboolean(L, 0); return 1; }
        gamely_daemon_webclient_ws_send(ctx->wc_id, data, len);
        lua_pushboolean(L, 1);
        return 1;
    }
    case 2:
        if (ctx) {
            ctx->lua_close = 1;
            gamely_daemon_webclient_ws_close(ctx->wc_id);
        }
        return 0;
    case 3:
        lua_pushboolean(L, ctx && ctx->is_ws && !ctx->lua_close);
        return 1;
    }
    return 0;
}

void gly_hook_luaopen_http(lua_State *L)
{
    lua_pushcfunction(L, lua_native_http_handler);
    lua_setglobal(L, "native_http_handler");

    lua_pushcfunction(L, lua_native_http_sock);
    lua_setglobal(L, "native_http_sock");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_ssl");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_callback");
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_func:native_http_handler", lua_native_http_handler, NULL);
    gecnd_registry("set", "lua_global_func:native_http_sock", lua_native_http_sock, NULL);
    gecnd_registry("set", "lua_global_bool:native_http_has_ssl", 1LLU, NULL);
    gecnd_registry("set", "lua_global_bool:native_http_has_callback", 1LLU, NULL);
}

__attribute__((destructor))
static void cleanup() {
    if (g_started) {
        gamely_daemon_webclient_stop();
        g_started = 0;
    }
}
