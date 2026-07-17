#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"
#include "gdweb.h"

static void on_core_engine(const char *key, void *value, void *usr)
{
    (void)key; (void)usr;
    gecnd_t *gly = (gecnd_t *)value;
    if (!gly) return;
    lua_getglobal(gly->L, "native_callback_http");
    if (lua_type(gly->L, -1) != LUA_TFUNCTION) {
        lua_pop(gly->L, 1);
        return;
    }
    gly->ref_native_callback_http = luaL_ref(gly->L, LUA_REGISTRYINDEX);
}

static void cb_push(gecnd_t *gly, int64_t lua_id, const char *evt)
{
    lua_rawgeti(gly->L, LUA_REGISTRYINDEX, gly->ref_native_callback_http);
    lua_pushinteger(gly->L, lua_id);
    lua_pushstring(gly->L, evt);
}

/* pcall com erro logado — um throw no handler do usuario nao pode sumir */
static void cb_call(lua_State *L, int nargs)
{
    if (lua_pcall(L, nargs, 0, 0)) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "[http] lua callback error: %s\n", err ? err : "?");
        lua_pop(L, 1);
    }
}

/* copia o resultado: o ponteiro de lua_tostring morre no lua_pop e o GC
 * pode recolher a string durante os proximos pcall — caller da free() */
static char *cb_get_str(gecnd_t *gly, int64_t lua_id, const char *evt)
{
    lua_State *L = gly->L;
    cb_push(gly, lua_id, evt);
    if (lua_pcall(L, 2, 1, 0)) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "[http] lua callback error: %s\n", err ? err : "?");
        lua_pop(L, 1);
        return NULL;
    }
    char *dup = NULL;
    if (lua_isstring(L, -1)) {
        size_t n;
        const char *s = lua_tolstring(L, -1, &n);
        dup = malloc(n + 1);
        if (dup) { memcpy(dup, s, n); dup[n] = '\0'; }
    }
    lua_pop(L, 1);
    return dup;
}

static void cb_promise(gecnd_t *gly, int64_t lua_id)
{
    cb_push(gly, lua_id, "async-promise");
    cb_call(gly->L, 2);
}

static void cb_resolve(gecnd_t *gly, int64_t lua_id)
{
    cb_push(gly, lua_id, "async-resolve");
    cb_call(gly->L, 2);
}

static void cb_error_immediate(lua_State *L, const char *msg)
{
    lua_getfield(L, 1, "set"); lua_pushstring(L, "body");  lua_pushstring(L, "");   lua_pcall(L, 2, 0, 0);
    lua_getfield(L, 1, "set"); lua_pushstring(L, "ok");    lua_pushboolean(L, 0);   lua_pcall(L, 2, 0, 0);
    lua_getfield(L, 1, "set"); lua_pushstring(L, "error"); lua_pushstring(L, msg);  lua_pcall(L, 2, 0, 0);
}

typedef struct {
    gecnd_t      *gly;
    int64_t       lua_id;    /* id do request no Lua (engine.http[lua_id])   */
    gdweb_id_t    con_id;    /* id da conexao no backend (gdweb) — nao vaza  */
    uint8_t       is_ws     : 1;
    uint8_t       lua_close : 1;
} req_ctx_t;

/**
 * @todo move to daemon WebLoop ownning pedings and *usr from frontend.
 */
/* precisa cobrir conns reais (64 do driver) + loopback (64 do webloop) */
#define MAX_PENDING 128
static req_ctx_t *g_pending[MAX_PENDING];

static int pending_add(req_ctx_t *ctx)
{
    for (int i = 0; i < MAX_PENDING; i++)
        if (!g_pending[i]) { g_pending[i] = ctx; return 1; }
    return 0;
}

static req_ctx_t *pending_find(int64_t lua_id)
{
    for (int i = 0; i < MAX_PENDING; i++)
        if (g_pending[i] && g_pending[i]->lua_id == lua_id) return g_pending[i];
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

static void on_status(gdweb_id_t id, int status, void *user)
{
    (void)id;
    req_ctx_t *ctx = user;
    cb_push(ctx->gly, ctx->lua_id, "set-status");
    lua_pushinteger(ctx->gly->L, status);
    cb_call(ctx->gly->L, 3);
}

static void on_data(gdweb_id_t id, const char *data, size_t len, void *user)
{
    (void)id;
    req_ctx_t *ctx = user;
    cb_push(ctx->gly, ctx->lua_id, "add-body-data");
    lua_pushlstring(ctx->gly->L, data, len);
    cb_call(ctx->gly->L, 3);
}

static void on_done(gdweb_id_t id, void *user)
{
    (void)id;
    req_ctx_t *ctx = user;
    if (!pending_remove(ctx)) return;   /* already finished by another path */
    cb_resolve(ctx->gly, ctx->lua_id);
    free(ctx);
}

static void on_error(gdweb_id_t id, const char *msg, void *user)
{
    (void)id;
    req_ctx_t *ctx = user;
    if (!pending_remove(ctx)) return;   /* already finished by another path */
    cb_push(ctx->gly, ctx->lua_id, "set-error");
    lua_pushstring(ctx->gly->L, msg);
    cb_call(ctx->gly->L, 3);
    cb_resolve(ctx->gly, ctx->lua_id);
    free(ctx);
}

static void on_ws_open(gdweb_id_t id, void *user)
{
    (void)id;
    req_ctx_t *ctx = user;
    cb_push(ctx->gly, ctx->lua_id, "set-status");
    lua_pushinteger(ctx->gly->L, 200);
    cb_call(ctx->gly->L, 3);
    cb_resolve(ctx->gly, ctx->lua_id);
}

static void on_ws_msg(gdweb_id_t id, const char *data, size_t len, void *user)
{
    (void)id;
    req_ctx_t *ctx = user;
    cb_push(ctx->gly, ctx->lua_id, "sock-message");
    lua_pushlstring(ctx->gly->L, data, len);
    cb_call(ctx->gly->L, 3);
}

static void on_ws_close(gdweb_id_t id, void *user)
{
    (void)id;
    req_ctx_t *ctx = user;
    if (!pending_remove(ctx)) return;   /* already finished by another path */
    if (!ctx->lua_close) {
        cb_push(ctx->gly, ctx->lua_id, "sock-event");
        lua_pushstring(ctx->gly->L, "disconnect");
        cb_call(ctx->gly->L, 3);
    }
    free(ctx);
}

static int g_started = 0;

static int lua_native_http_handler(lua_State *L)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, GLY_REGISTRYINDEX);
    gecnd_t *gly = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!gly || !gly->L) {
        cb_error_immediate(L, "[core:error] core not ready!");
        return 0;
    }

    if (!g_started) {
        if (!gly->loop) {
            cb_error_immediate(L, "[core:error] libuv is not started!");
            return 0;
        }
        gdweb_control_client()->start(gly->loop);
        g_started = 1;
    }

    /* id do request no Lua (engine.http[lua_id]) — obrigatorio e distinto
     * do con_id, que pertence ao backend e nunca sobe para o Lua */
    int64_t lua_id = luaL_checkinteger(L, 2);
    if (!lua_id) {
        cb_error_immediate(L, "missing request id");
        return 0;
    }

    char *url    = cb_get_str(gly, lua_id, "get-fullurl");
    char *method = cb_get_str(gly, lua_id, "get-method");
    int   is_ws  = method && strcmp(method, "SOCK") == 0;
    char *body    = NULL;
    char *upgrade = NULL;

    if (is_ws) {
        upgrade = cb_get_str(gly, lua_id, "get-sock-upgrade");
    } else {
        body = cb_get_str(gly, lua_id, "get-body");
    }

    /**
     * @todo iterate request headers via get-header-count / get-header-name /
     *       get-header-data and forward them to the driver. Will require
     *       extending gdweb_http_req_t with header_names/header_values/header_count
     *       (shared between HTTP and WS) and adding header injection in
     *       LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER inside driver_warmcat.c.
     */

    if (!url) {
        cb_error_immediate(L, "missing url");
        goto cleanup_strings;
    }

    req_ctx_t *ctx = malloc(sizeof(req_ctx_t));
    if (!ctx) { cb_error_immediate(L, "out of memory"); goto cleanup_strings; }
    ctx->gly       = gly;
    ctx->lua_id    = lua_id;
    ctx->con_id    = 0;
    ctx->is_ws     = is_ws;
    ctx->lua_close = 0;
    if (!pending_add(ctx)) {
        free(ctx);
        cb_error_immediate(L, "too many pending requests");
        goto cleanup_strings;
    }

    gdweb_id_t con_id;
    if (is_ws) {
        con_id = gdweb_control_client()->ws_connect(
            url, upgrade,
            on_ws_open, on_ws_msg, on_ws_close, on_error,
            ctx
        );
    } else {
        gdweb_http_req_t req = { .method = method, .body = body, .body_len = body ? strlen(body) : 0 };
        con_id = gdweb_control_client()->http(
            url, &req,
            on_status, on_data, on_done, on_error,
            ctx
        );
    }

    if (!con_id) {
        if (pending_remove(ctx)) {
            free(ctx);
            cb_error_immediate(L, "failed to connect");
        }
        goto cleanup_strings;
    }
    ctx->con_id = con_id;

    cb_promise(gly, lua_id);

cleanup_strings:
    free(url);
    free(method);
    free(body);
    free(upgrade);
    return 0;
}

static int lua_native_http_sock(lua_State *L)
{
    int64_t    lua_id = luaL_checkinteger(L, 1);
    int        op     = (int)luaL_checkinteger(L, 2);
    req_ctx_t *ctx    = pending_find(lua_id);

    switch (op) {
    case 1: {
        size_t      len  = 0;
        const char *data = luaL_checklstring(L, 3, &len);
        if (!ctx || !ctx->is_ws) { lua_pushboolean(L, 0); return 1; }
        gdweb_control_client()->send(ctx->con_id, data, len);
        lua_pushboolean(L, 1);
        return 1;
    }
    case 2:
        if (ctx) {
            ctx->lua_close = 1;
            gdweb_control_client()->close(ctx->con_id);
        }
        return 0;
    case 3:
        lua_pushboolean(L, ctx && ctx->is_ws && !ctx->lua_close);
        return 1;
    }
    return 0;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("hook", "core:engine", (void *)on_core_engine, NULL);
    gecnd_registry("set", "lua_global_func:native_http_handler", lua_native_http_handler, NULL);
    gecnd_registry("set", "lua_global_func:native_http_sock", lua_native_http_sock, NULL);
    gecnd_registry("set", "lua_global_value:native_http_has_ssl+$b", (void *)1, NULL);
    gecnd_registry("set", "lua_global_value:native_http_has_callback+$b", (void *)1, NULL);
}

__attribute__((destructor))
static void cleanup() {
    if (g_started) {
        gdweb_control_client()->stop();
        g_started = 0;
    }
}
