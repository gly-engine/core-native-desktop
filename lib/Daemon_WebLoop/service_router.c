#include "gecnd.h"
#include <string.h>
#include <stdio.h>

#define MAX_ROUTES 64

typedef enum { WLR_HTTP, WLR_WS, WLR_STREAM, WLR_PROXY } wlr_type_t;

typedef struct {
    char            path[128];
    wlr_type_t      type;
    gly_http_cb_t   http_cb;
    gly_ws_cb_t     ws_cb;
    gly_stream_cb_t stream_cb;
    char            content_type[64];
    char            proxy_to[128];
} wl_route_t;

static wl_route_t s_routes[MAX_ROUTES];
static int        s_count = 0;

/* -----------------------------------------------------------------------
 * Externs — route registration into WebServer (implemented in driver_warmcat.c)
 * ---------------------------------------------------------------------- */
extern void gamely_daemon_webserver_route_http   (const char *, gly_http_cb_t);
extern void gamely_daemon_webserver_route_ws     (const char *, gly_ws_cb_t);
extern void gamely_daemon_webserver_route_stream (const char *, const char *, gly_stream_cb_t);

/* -----------------------------------------------------------------------
 * Externs — service callbacks (implemented in service_*.c, no shared header)
 * ---------------------------------------------------------------------- */
extern void http_rc              (const gly_http_req_t *);
extern void ws_rc                (const gly_ws_req_t *);
extern void service_stream_client_cb (gly_req_id_t, bool);

/* -----------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */
static wl_route_t *find_route(const char *path, wlr_type_t type)
{
    for (int i = 0; i < s_count; i++) {
        if (s_routes[i].type == type && strcmp(s_routes[i].path, path) == 0)
            return &s_routes[i];
    }
    return NULL;
}

static wl_route_t *insert_route(const char *path, wlr_type_t type)
{
    wl_route_t *r = find_route(path, type);
    if (r) return r;
    if (s_count >= MAX_ROUTES) {
        fprintf(stderr, "[webloop] MAX_ROUTES atingido\n");
        return NULL;
    }
    r = &s_routes[s_count++];
    memset(r, 0, sizeof(*r));
    strncpy(r->path, path, sizeof(r->path) - 1);
    r->type = type;
    return r;
}

/* -----------------------------------------------------------------------
 * wl_router_find_* — called by service_loopback.c via extern
 * ---------------------------------------------------------------------- */
gly_http_cb_t wl_router_find_http(const char *path)
{
    wl_route_t *r = find_route(path, WLR_HTTP);
    return r ? r->http_cb : NULL;
}

gly_ws_cb_t wl_router_find_ws(const char *path)
{
    wl_route_t *r = find_route(path, WLR_WS);
    return r ? r->ws_cb : NULL;
}

gly_stream_cb_t wl_router_find_stream(const char *path)
{
    wl_route_t *r = find_route(path, WLR_STREAM);
    return r ? r->stream_cb : NULL;
}

/* -----------------------------------------------------------------------
 * Public API — gamely_daemon_webloop_route_*
 * ---------------------------------------------------------------------- */
void gamely_daemon_webloop_route_http(const char *path, gly_http_cb_t cb)
{
    wl_route_t *r = insert_route(path, WLR_HTTP);
    if (!r) return;
    r->http_cb = cb;
    gamely_daemon_webserver_route_http(path, cb);
}

void gamely_daemon_webloop_route_ws(const char *path, gly_ws_cb_t cb)
{
    wl_route_t *r = insert_route(path, WLR_WS);
    if (!r) return;
    r->ws_cb = cb;
    gamely_daemon_webserver_route_ws(path, cb);
}

void gamely_daemon_webloop_route_stream(const char *path, const char *content_type,
                                        gly_stream_cb_t cb)
{
    wl_route_t *r = insert_route(path, WLR_STREAM);
    if (!r) return;
    r->stream_cb = cb;
    strncpy(r->content_type,
            (content_type && content_type[0]) ? content_type : "video/mp2t",
            sizeof(r->content_type) - 1);
    gamely_daemon_webserver_route_stream(path, content_type, cb);
}

void gamely_daemon_webloop_route_proxy(const char *from, const char *to)
{
    wl_route_t *r = insert_route(from, WLR_PROXY);
    if (!r) return;
    strncpy(r->proxy_to, to, sizeof(r->proxy_to) - 1);
}

/* -----------------------------------------------------------------------
 * wl_router_init — register built-in service routes; called from webloop_start
 * ---------------------------------------------------------------------- */
void wl_router_init(void)
{
    gamely_daemon_webloop_route_http("/rc", http_rc);
    gamely_daemon_webloop_route_ws  ("/rc", ws_rc);
    gamely_daemon_webloop_route_stream("/stream", "video/mp2t", service_stream_client_cb);
}
