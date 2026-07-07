#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
int luaL_loadstring(lua_State *L, const char *s);
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"
#include "gdweb.h"

#define LUA_QUEUE_CAP 16
#define LUA_OUT_CAP   16384

typedef struct {
    gdweb_id_t id;
    char        *code;
} lua_job_t;

static lua_job_t s_queue[LUA_QUEUE_CAP];
static int       s_head = 0;
static int       s_tail = 0;

static int queue_count(void) {
    return (s_tail - s_head + LUA_QUEUE_CAP) % LUA_QUEUE_CAP;
}

static void out_append(char *buf, size_t cap, size_t *len, const char *s, size_t n) {
    if (*len >= cap - 1) return;
    size_t room = cap - 1 - *len;
    if (n > room) n = room;
    memcpy(buf + *len, s, n);
    *len += n;
    buf[*len] = '\0';
}

static void out_append_value(lua_State *L, int idx, char *buf, size_t cap, size_t *len) {
    switch (lua_type(L, idx)) {
        case LUA_TSTRING: {
            size_t n; const char *s = lua_tolstring(L, idx, &n);
            out_append(buf, cap, len, s, n);
            break;
        }
        case LUA_TNUMBER: {
            const char *s = lua_tostring(L, idx);
            out_append(buf, cap, len, s, strlen(s));
            break;
        }
        case LUA_TBOOLEAN: {
            const char *s = lua_toboolean(L, idx) ? "true" : "false";
            out_append(buf, cap, len, s, strlen(s));
            break;
        }
        case LUA_TNIL:
            out_append(buf, cap, len, "nil", 3);
            break;
        default: {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%s: %p",
                             lua_typename(L, lua_type(L, idx)), lua_topointer(L, idx));
            out_append(buf, cap, len, tmp, n > 0 ? (size_t)n : 0);
            break;
        }
    }
}

static int repl_load(lua_State *L, const char *code) {
    size_t n = strlen(code);
    char  *expr = malloc(n + 8);
    if (expr) {
        memcpy(expr, "return ", 7);
        memcpy(expr + 7, code, n + 1);
        int r = luaL_loadstring(L, expr);
        free(expr);
        if (r == 0) return 0;
        lua_pop(L, 1);
    }
    return luaL_loadstring(L, code);
}

static void repl_exec(lua_State *L, const char *code, char *out, size_t cap) {
    size_t len = 0;
    out[0] = '\0';
    int base = lua_gettop(L);

    if (repl_load(L, code) != 0) {
        const char *err = lua_tostring(L, -1);
        out_append(out, cap, &len, err ? err : "compile error",
                   err ? strlen(err) : 13);
        lua_settop(L, base);
        return;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) {
        const char *err = lua_tostring(L, -1);
        out_append(out, cap, &len, err ? err : "runtime error",
                   err ? strlen(err) : 13);
        lua_settop(L, base);
        return;
    }

    int nres = lua_gettop(L) - base;
    for (int i = 1; i <= nres; i++) {
        if (i > 1) out_append(out, cap, &len, "\t", 1);
        out_append_value(L, base + i, out, cap, &len);
    }
    lua_settop(L, base);
}

static const char s_html[] =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"/>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>"
"<title>Lua REPL</title><style>"
"body{margin:0;background:#111;color:#ddd;font-family:monospace;display:flex;"
"flex-direction:column;height:100vh;}"
"#out{flex:1;overflow:auto;padding:10px;white-space:pre-wrap;font-size:13px;}"
"#out .in{color:#6cf;}#out .err{color:#f66;}#out .res{color:#9f9;}"
"#in{border:0;border-top:1px solid #333;background:#000;color:#ddd;"
"font-family:monospace;font-size:14px;padding:10px;outline:none;}"
"</style></head><body>"
"<div id=\"out\"></div>"
"<input id=\"in\" placeholder=\"lua &gt;  (enter para executar)\" autofocus/>"
"<script>"
"const out=document.getElementById('out'),inp=document.getElementById('in');"
"const log=(t,c)=>{const d=document.createElement('div');d.className=c;d.textContent=t;"
"out.appendChild(d);out.scrollTop=out.scrollHeight;};"
"const ws=new WebSocket('ws://'+location.host+'/lua','ws');"
"ws.onopen=()=>log('-- conectado --','res');"
"ws.onclose=()=>log('-- desconectado --','err');"
"ws.onmessage=e=>log(e.data,'res');"
"inp.addEventListener('keydown',ev=>{if(ev.key==='Enter'&&inp.value.trim()){"
"log('> '+inp.value,'in');if(ws.readyState===1)ws.send(inp.value);inp.value='';}});"
"</script></body></html>";

static void http_lua(const gdweb_http_req_t *req)
{
    gdweb_value_t ct = { .str = "text/html; charset=utf-8" };
    gdweb_control_server()->http(req->id, GDWEB_HTTP_CONTENT_TYPE, &ct);
    gdweb_control_server()->send(req->id, s_html, sizeof(s_html) - 1);
}

static void ws_lua(const gdweb_ws_req_t *req)
{
    if (req->event != GDWEB_WS_MESSAGE || req->len < 1) return;

    if (queue_count() >= LUA_QUEUE_CAP - 1) {
        static const char busy[] = "error: lua queue full";
        gdweb_control_server()->send(req->id, busy, sizeof(busy) - 1);
        return;
    }

    char *code = malloc(req->len + 1);
    if (!code) return;
    memcpy(code, req->data, req->len);
    code[req->len] = '\0';

    s_queue[s_tail].id   = req->id;
    s_queue[s_tail].code = code;
    s_tail = (s_tail + 1) % LUA_QUEUE_CAP;
}

void gdweb_lua_tick(void)
{
    if (s_head == s_tail) return;

    gecnd_t *root = gecnd_get_root();
    if (!root || !root->L) return;

    static char out[LUA_OUT_CAP];

    while (s_head != s_tail) {
        lua_job_t job = s_queue[s_head];
        s_head = (s_head + 1) % LUA_QUEUE_CAP;

        repl_exec(root->L, job.code, out, sizeof(out));
        gdweb_control_server()->send(job.id, out, strlen(out));
        free(job.code);
    }
}

__attribute__((constructor))
static void register_lua_routes(void)
{
    gecnd_registry("set", "web_http_route:lua", (void *)http_lua, NULL);
    gecnd_registry("set", "web_ws_route:lua",   (void *)ws_lua,   NULL);
}
