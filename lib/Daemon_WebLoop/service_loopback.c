#include "gecnd.h"
#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define WL_MAX_CONNS 64

typedef enum { WL_HTTP, WL_WS } wl_type_t;

typedef struct {
    gly_req_id_t        id;
    wl_type_t           type;
    int                 active;

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
static uint32_t   s_next_id = 0x80000000u;

/* IDs de loopback usam bit 31 para não colidir com IDs LWS */
static wl_conn_t *wl_conn_alloc(void)
{
    for (int i = 0; i < WL_MAX_CONNS; i++) {
        if (!s_pool[i].active) {
            memset(&s_pool[i], 0, sizeof(s_pool[i]));
            s_pool[i].active = 1;
            s_pool[i].id     = s_next_id++;
            if (s_next_id == 0) s_next_id = 0x80000000u;
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
    for (int i = 0; i < WL_MAX_CONNS; i++) {
        if (s_pool[i].active && s_pool[i].id == id) return &s_pool[i];
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Extern prototypes to route lookup (implemented in service_router.c)
 * ---------------------------------------------------------------------- */
extern gly_http_cb_t   wl_router_find_http   (const char *path);
extern gly_ws_cb_t     wl_router_find_ws     (const char *path);
extern gly_stream_cb_t wl_router_find_stream (const char *path);

/* -----------------------------------------------------------------------
 * Timer callbacks — fire on next loop iteration (delay=0)
 * ---------------------------------------------------------------------- */
static void _http_fire(uv_timer_t *h)
{
    wl_conn_t *c = (wl_conn_t *)h->data;
    uv_timer_stop(h);

    gly_http_cb_t route_cb = wl_router_find_http(c->path);
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
    /* response arrives via webloop_respond_http */
}

static void _ws_fire(uv_timer_t *h)
{
    wl_conn_t *c = (wl_conn_t *)h->data;
    uv_timer_stop(h);

    gly_ws_cb_t route_cb = wl_router_find_ws(c->path);
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
 * webloop_respond_* — called by WebServer response functions
 * Returns 1 if the id belongs to a loopback conn (WebServer must skip LWS).
 * ---------------------------------------------------------------------- */
int webloop_respond_http(gly_req_id_t id, int status, const char *ct,
                         const char *body, size_t len)
{
    (void)ct;
    wl_conn_t *c = wl_conn_by_id(id);
    if (!c || c->type != WL_HTTP) return 0;

    if (c->on_status) c->on_status(id, status, c->user);
    if (c->on_data && body && len) c->on_data(id, body, len, c->user);
    if (c->on_done) c->on_done(id, c->user);

    wl_conn_free(c);
    return 1;
}

int webloop_respond_ws(gly_req_id_t id, const char *data, size_t len)
{
    wl_conn_t *c = wl_conn_by_id(id);
    if (!c || c->type != WL_WS) return 0;
    if (c->on_ws_msg) c->on_ws_msg(id, data, len, c->user);
    return 1;
}

int webloop_respond_stream(gly_req_id_t id, const uint8_t *buf, int size)
{
    wl_conn_t *c = wl_conn_by_id(id);
    if (!c || c->type != WL_HTTP) return 0;
    if (c->on_data) c->on_data(id, (const char *)buf, (size_t)size, c->user);
    return 1;
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

    c->type       = WL_WS;
    c->on_ws_open = on_open;
    c->on_ws_msg  = on_msg;
    c->on_ws_close = on_close;
    c->on_error   = on_error;
    c->user       = user;
    strncpy(c->path, path ? path : "/", sizeof(c->path) - 1);

    c->timer.data = c;
    uv_timer_init(s_loop, &c->timer);
    uv_timer_start(&c->timer, _ws_fire, 0, 0);

    return c->id;
}

/* -----------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */
extern void wl_router_init(void);

void gamely_daemon_webloop_start(void *loop)
{
    if (s_loop) return;
    s_loop = (uv_loop_t *)loop;
    wl_router_init();
}

void gamely_daemon_webloop_stop(void)
{
    for (int i = 0; i < WL_MAX_CONNS; i++) {
        if (s_pool[i].active) {
            uv_timer_stop(&s_pool[i].timer);
            wl_conn_free(&s_pool[i]);
        }
    }
    s_loop = NULL;
}
