#include "gecnd.h"
#include "gdwsl.h"
#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * Driver transport functions (implemented in Daemon_WebServer/driver_warmcat.c)
 * ---------------------------------------------------------------------- */
extern void driver_http_set     (uint32_t conn_id, gdwsl_http_cmd_t cmd,
                                  const gdwsl_value_t *value);
extern void driver_send         (uint32_t conn_id, const char *data, size_t len);
extern void driver_ws_send_all  (const char *path, const char *data, size_t len,
                                  uint32_t exclude_conn_id);
extern void driver_server_start (void *loop, int port);
extern void driver_server_stop  (void);

/* -----------------------------------------------------------------------
 * req_id ↔ conn_id mapping for real (non-loopback) connections
 * ---------------------------------------------------------------------- */
#define WL_MAX_REAL 128

typedef struct {
    uint32_t      conn_id;
    gly_req_id_t  req_id;
    int           active;
} wl_req_map_t;

static wl_req_map_t s_req_map[WL_MAX_REAL];
static uint32_t     s_next_real_id = 1; /* real req_ids: 1..0x7FFFFFFF */

gly_req_id_t webloop_alloc_req(uint32_t conn_id)
{
    for (int i = 0; i < WL_MAX_REAL; i++) {
        if (s_req_map[i].active) continue;
        if (s_next_real_id >= 0x80000000u) s_next_real_id = 1;
        s_req_map[i].conn_id = conn_id;
        s_req_map[i].req_id  = s_next_real_id++;
        s_req_map[i].active  = 1;
        return s_req_map[i].req_id;
    }
    fprintf(stderr, "[webloop] req_map full\n");
    return 0;
}

void webloop_free_req(uint32_t conn_id)
{
    for (int i = 0; i < WL_MAX_REAL; i++) {
        if (s_req_map[i].active && s_req_map[i].conn_id == conn_id) {
            s_req_map[i].active = 0;
            return;
        }
    }
}

static uint32_t req_to_conn(gly_req_id_t req_id)
{
    for (int i = 0; i < WL_MAX_REAL; i++)
        if (s_req_map[i].active && s_req_map[i].req_id == req_id)
            return s_req_map[i].conn_id;
    return 0;
}

/* -----------------------------------------------------------------------
 * Loopback connection pool (WebClient-originated requests)
 * IDs use bit 31 to distinguish from real req_ids
 * ---------------------------------------------------------------------- */
#define WL_MAX_CONNS 64

typedef enum { WL_HTTP, WL_WS } wl_type_t;

typedef struct {
    gly_req_id_t        id;
    wl_type_t           type;
    int                 active;
    int                 status;   /* GDWSL_HTTP_STATUS pendente (0 = 200) */

    char                path[256];
    char                body[4096];
    size_t              body_len;
    const char         *method;

    gly_wc_status_cb    on_status;
    gly_wc_data_cb      on_data;
    gly_wc_done_cb      on_done;
    gly_wc_error_cb     on_error;
    gly_wc_ws_open_cb   on_ws_open;
    gly_wc_ws_msg_cb    on_ws_msg;
    gly_wc_ws_close_cb  on_ws_close;
    void               *user;

    uv_timer_t          timer;
} wl_conn_t;

static wl_conn_t  s_pool[WL_MAX_CONNS];
static uv_loop_t *s_loop = NULL;
static uint32_t   s_next_lb_id = 0x80000000u;

static wl_conn_t *wl_conn_alloc(void)
{
    for (int i = 0; i < WL_MAX_CONNS; i++) {
        if (!s_pool[i].active) {
            memset(&s_pool[i], 0, sizeof(s_pool[i]));
            s_pool[i].active = 1;
            s_pool[i].id     = s_next_lb_id++;
            if (s_next_lb_id == 0) s_next_lb_id = 0x80000000u;
            return &s_pool[i];
        }
    }
    return NULL;
}

static void wl_conn_free(wl_conn_t *c)
{
    if (!c) return;
    c->active = 0;
}

static wl_conn_t *wl_conn_by_id(gly_req_id_t id)
{
    if (!(id & 0x80000000u)) return NULL;
    for (int i = 0; i < WL_MAX_CONNS; i++)
        if (s_pool[i].active && s_pool[i].id == id) return &s_pool[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * Route lookup — registry-backed: "/foo/bar" vira "web_http_route:(foo:bar)"
 * ---------------------------------------------------------------------- */
typedef struct {
    void *cb;
} route_ctx_t;

static void route_handler(const char *key, void *value, void *usr) {
    (void)key;
    ((route_ctx_t *)usr)->cb = value;
}

static void *route_find(const char *kind, const char *path)
{
    char   key[192];
    size_t n = (size_t)snprintf(key, sizeof(key), "%s:(", kind);
    const char *p = (path && *path == '/') ? path + 1 : (path ? path : "");
    while (*p && *p != '?' && n < sizeof(key) - 2) {
        key[n++] = (*p == '/') ? ':' : *p;
        p++;
    }
    key[n++] = ')';
    key[n]   = '\0';

    route_ctx_t ctx = { NULL };
    gecnd_registry("get", key, (void *)route_handler, &ctx);
    return ctx.cb;
}

gly_http_cb_t webloop_route_http(const char *path)
{
    return (gly_http_cb_t)route_find("web_http_route", path);
}

gly_ws_cb_t webloop_route_ws(const char *path)
{
    return (gly_ws_cb_t)route_find("web_ws_route", path);
}

gly_stream_cb_t webloop_route_stream(const char *path)
{
    return (gly_stream_cb_t)route_find("web_stream_route", path);
}

/* -----------------------------------------------------------------------
 * Timer callbacks — fire on next loop iteration (delay=0, never inline)
 * ---------------------------------------------------------------------- */
static void _http_fire(uv_timer_t *h)
{
    wl_conn_t *c = (wl_conn_t *)h->data;
    uv_timer_stop(h);

    gly_http_cb_t route_cb = webloop_route_http(c->path);
    if (!route_cb) {
        if (c->on_error) c->on_error(c->id, "no route", c->user);
        wl_conn_free(c);
        return;
    }

    gly_http_req_t req = {
        .id       = c->id,
        .method   = c->method ? c->method : "GET",
        .path     = c->path,
        .body     = c->body_len ? c->body : NULL,
        .body_len = c->body_len
    };
    route_cb(&req);
    /* response arrives via gdwsl_control_server()->send() */
}

static void _ws_fire(uv_timer_t *h)
{
    wl_conn_t *c = (wl_conn_t *)h->data;
    uv_timer_stop(h);

    gly_ws_cb_t route_cb = webloop_route_ws(c->path);
    if (!route_cb) {
        if (c->on_error) c->on_error(c->id, "no route", c->user);
        wl_conn_free(c);
        return;
    }

    if (c->on_ws_open) c->on_ws_open(c->id, c->user);

    gly_ws_req_t req = { .id = c->id, .event = GLY_WS_OPEN };
    route_cb(&req);
}

/* -----------------------------------------------------------------------
 * Server control — gdwsl_control_server()
 * WebLoop decides: loopback delivery or delegate to driver transport.
 * ---------------------------------------------------------------------- */
static void sv_http(gly_req_id_t id, gdwsl_http_cmd_t cmd, const gdwsl_value_t *value)
{
    if (id & 0x80000000u) {
        wl_conn_t *c = wl_conn_by_id(id);
        if (c && cmd == GDWSL_HTTP_STATUS && value)
            c->status = (int)value->i64;
        return;
    }
    uint32_t conn_id = req_to_conn(id);
    if (conn_id) driver_http_set(conn_id, cmd, value);
}

static void sv_send(gly_req_id_t id, const char *data, size_t len)
{
    if (id & 0x80000000u) {
        wl_conn_t *c = wl_conn_by_id(id);
        if (!c) return;
        if (c->type == WL_WS) {
            if (c->on_ws_msg) c->on_ws_msg(id, data, len, c->user);
            return;
        }
        if (c->on_status) c->on_status(id, c->status ? c->status : 200, c->user);
        if (c->on_data && data && len) c->on_data(id, data, len, c->user);
        if (c->on_done) c->on_done(id, c->user);
        wl_conn_free(c);
        return;
    }
    uint32_t conn_id = req_to_conn(id);
    if (conn_id) driver_send(conn_id, data, len);
}

static void sv_send_all(const char *path, const char *data, size_t len,
                        gly_req_id_t exclude_id)
{
    uint32_t exclude_conn = (exclude_id && !(exclude_id & 0x80000000u))
                            ? req_to_conn(exclude_id) : 0;
    driver_ws_send_all(path, data, len, exclude_conn);
}

static const gdwsl_server_t s_server = {
    .http     = sv_http,
    .send     = sv_send,
    .send_all = sv_send_all,
    .start    = driver_server_start,
    .stop     = driver_server_stop,
};

const gdwsl_server_t *gdwsl_control_server(void)
{
    return &s_server;
}

/* -----------------------------------------------------------------------
 * webloop_client_ws_send / webloop_client_ws_close
 * Called by WebClient for loopback WS (client→server direction).
 * ---------------------------------------------------------------------- */
void webloop_client_ws_send(gly_req_id_t id, const char *data, size_t len)
{
    wl_conn_t *c = wl_conn_by_id(id);
    if (!c || c->type != WL_WS) return;

    gly_ws_cb_t route_cb = webloop_route_ws(c->path);
    if (!route_cb) return;

    gly_ws_req_t req = { .id = id, .event = GLY_WS_MESSAGE, .data = data, .len = len };
    route_cb(&req);
}

void webloop_client_ws_close(gly_req_id_t id)
{
    wl_conn_t *c = wl_conn_by_id(id);
    if (!c || c->type != WL_WS) return;

    gly_ws_cb_t route_cb = webloop_route_ws(c->path);
    if (route_cb) {
        gly_ws_req_t req = { .id = id, .event = GLY_WS_CLOSE };
        route_cb(&req);
    }
    if (c->on_ws_close) c->on_ws_close(id, c->user);
    wl_conn_free(c);
}

/* -----------------------------------------------------------------------
 * webloop_http_request / webloop_ws_connect — called by WebClient driver
 * ---------------------------------------------------------------------- */
gly_req_id_t webloop_http_request(const char *path, const char *method,
                                   const char *body, size_t body_len,
                                   gly_wc_status_cb on_status,
                                   gly_wc_data_cb   on_data,
                                   gly_wc_done_cb   on_done,
                                   gly_wc_error_cb  on_error,
                                   void            *user)
{
    if (!s_loop) {
        if (on_error) on_error(0, "webloop not started", user);
        return 0;
    }

    wl_conn_t *c = wl_conn_alloc();
    if (!c) {
        if (on_error) on_error(0, "loopback pool exhausted", user);
        return 0;
    }

    c->type      = WL_HTTP;
    c->method    = method;
    c->on_status = on_status;
    c->on_data   = on_data;
    c->on_done   = on_done;
    c->on_error  = on_error;
    c->user      = user;
    strncpy(c->path, path ? path : "/", sizeof(c->path) - 1);
    if (body && body_len) {
        size_t n = body_len < sizeof(c->body) ? body_len : sizeof(c->body);
        memcpy(c->body, body, n);
        c->body_len = n;
    }

    c->timer.data = c;
    uv_timer_init(s_loop, &c->timer);
    uv_timer_start(&c->timer, _http_fire, 0, 0);

    return c->id;
}

gly_req_id_t webloop_ws_connect(const char *path,
                                 gly_wc_ws_open_cb  on_open,
                                 gly_wc_ws_msg_cb   on_msg,
                                 gly_wc_ws_close_cb on_close,
                                 gly_wc_error_cb    on_error,
                                 void              *user)
{
    if (!s_loop) {
        if (on_error) on_error(0, "webloop not started", user);
        return 0;
    }

    wl_conn_t *c = wl_conn_alloc();
    if (!c) {
        if (on_error) on_error(0, "loopback pool exhausted", user);
        return 0;
    }

    c->type        = WL_WS;
    c->on_ws_open  = on_open;
    c->on_ws_msg   = on_msg;
    c->on_ws_close = on_close;
    c->on_error    = on_error;
    c->user        = user;
    strncpy(c->path, path ? path : "/", sizeof(c->path) - 1);

    c->timer.data = c;
    uv_timer_init(s_loop, &c->timer);
    uv_timer_start(&c->timer, _ws_fire, 0, 0);

    return c->id;
}

/* -----------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */
void gdwsl_loop_start(void *loop)
{
    if (s_loop) return;
    s_loop = (uv_loop_t *)loop;
}

void gdwsl_loop_stop(void)
{
    for (int i = 0; i < WL_MAX_CONNS; i++) {
        if (s_pool[i].active) {
            uv_timer_stop(&s_pool[i].timer);
            wl_conn_free(&s_pool[i]);
        }
    }
    memset(s_req_map, 0, sizeof(s_req_map));
    s_loop = NULL;
}

__attribute__((constructor))
static void register_loopback_functions(void)
{
    gecnd_registry("set", "function:gdwsl_control_server", (void *)gdwsl_control_server, NULL);
}
