#include "gecnd.h"
#include "gdweb.h"
#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * Driver transport functions — resolvidas via registry "web_driver:*"
 * (registradas pelos drivers em lib/Daemon_Web/driver_*_uv_*.c)
 * ---------------------------------------------------------------------- */
static struct {
    void (*server_start)   (void *loop, int port);
    void (*server_stop)    (void);
    void (*server_http)    (uint32_t conn_id, gdweb_http_cmd_t cmd,
                            const gdweb_value_t *value);
    void (*server_send)    (uint32_t conn_id, const char *data, size_t len);
    void (*server_send_all)(const char *path, const char *data, size_t len,
                            uint32_t exclude_conn_id);

    gdweb_id_t (*client_http)(const char *url, gdweb_http_req_t *req,
                              gdweb_status_cb_t on_status, gdweb_data_cb_t on_data,
                              gdweb_done_cb_t on_done, gdweb_error_cb_t on_error,
                              void *user);
    gdweb_id_t (*client_ws)  (const char *url, const char *protocol,
                              gdweb_ws_open_cb_t on_open, gdweb_ws_msg_cb_t on_msg,
                              gdweb_ws_close_cb_t on_close, gdweb_error_cb_t on_error,
                              void *user);
    void (*client_send) (gdweb_id_t id, const char *data, size_t len);
    void (*client_close)(gdweb_id_t id);
    void (*client_start)(void *loop);
    void (*client_stop) (void);

    bool bound;
} drv;

static void drv_get(const char *fn, void *dst)
{
    char key[64];
    snprintf(key, sizeof(key), "web_driver:%s", fn);
    gecnd_registry("get", key, dst, NULL);
}

static void drv_bind(void)
{
    if (drv.bound) return;
    drv.bound = true;
    drv_get("server_start",    &drv.server_start);
    drv_get("server_stop",     &drv.server_stop);
    drv_get("server_http",     &drv.server_http);
    drv_get("server_send",     &drv.server_send);
    drv_get("server_send_all", &drv.server_send_all);
    drv_get("client_http",     &drv.client_http);
    drv_get("client_ws",       &drv.client_ws);
    drv_get("client_send",     &drv.client_send);
    drv_get("client_close",    &drv.client_close);
    drv_get("client_start",    &drv.client_start);
    drv_get("client_stop",     &drv.client_stop);
}

/* -----------------------------------------------------------------------
 * req_id ↔ conn_id mapping for real (non-loopback) connections
 * ---------------------------------------------------------------------- */
#define WL_MAX_REAL 128

typedef struct {
    uint32_t      conn_id;
    gdweb_id_t  req_id;
    int           active;
} wl_req_map_t;

static wl_req_map_t s_req_map[WL_MAX_REAL];
static uint32_t     s_next_real_id = 1; /* real req_ids: 1..0x7FFFFFFF */

gdweb_id_t web_alloc_req(uint32_t conn_id)
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

void web_free_req(uint32_t conn_id)
{
    for (int i = 0; i < WL_MAX_REAL; i++) {
        if (s_req_map[i].active && s_req_map[i].conn_id == conn_id) {
            s_req_map[i].active = 0;
            return;
        }
    }
}

static uint32_t req_to_conn(gdweb_id_t req_id)
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
    gdweb_id_t        id;
    wl_type_t           type;
    int                 active;
    int                 status;   /* GDWEB_HTTP_STATUS pendente (0 = 200) */

    char                path[256];
    char                body[4096];
    size_t              body_len;
    const char         *method;

    gdweb_status_cb_t    on_status;
    gdweb_data_cb_t      on_data;
    gdweb_done_cb_t      on_done;
    gdweb_error_cb_t     on_error;
    gdweb_ws_open_cb_t   on_ws_open;
    gdweb_ws_msg_cb_t    on_ws_msg;
    gdweb_ws_close_cb_t  on_ws_close;
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

static wl_conn_t *wl_conn_by_id(gdweb_id_t id)
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

gdweb_http_cb_t web_route_http(const char *path)
{
    return (gdweb_http_cb_t)route_find("web_http_route", path);
}

gdweb_ws_cb_t web_route_ws(const char *path)
{
    return (gdweb_ws_cb_t)route_find("web_ws_route", path);
}

gdweb_stream_cb_t web_route_stream(const char *path)
{
    return (gdweb_stream_cb_t)route_find("web_stream_route", path);
}

/* -----------------------------------------------------------------------
 * Static mounts — registry-backed: "set" "web_http_path:rc" "./rc2"
 * faz "/rc/index.html" resolver para o arquivo "./rc2/index.html"
 * ---------------------------------------------------------------------- */
static const char *web_mime_by_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    dot++;
    if (!strcmp(dot, "html") || !strcmp(dot, "htm")) return "text/html";
    if (!strcmp(dot, "js"))   return "application/javascript";
    if (!strcmp(dot, "css"))  return "text/css";
    if (!strcmp(dot, "json")) return "application/json";
    if (!strcmp(dot, "png"))  return "image/png";
    if (!strcmp(dot, "jpg") || !strcmp(dot, "jpeg")) return "image/jpeg";
    if (!strcmp(dot, "gif"))  return "image/gif";
    if (!strcmp(dot, "svg"))  return "image/svg+xml";
    if (!strcmp(dot, "ico"))  return "image/x-icon";
    if (!strcmp(dot, "wasm")) return "application/wasm";
    if (!strcmp(dot, "txt"))  return "text/plain";
    return "application/octet-stream";
}

static bool web_http_path_resolve(const char *path, char *out, size_t cap)
{
    if (!path || *path != '/') return false;
    const char *seg = path + 1;
    const char *end = seg;
    while (*end && *end != '/' && *end != '?') end++;
    if (end == seg) return false;

    char key[128];
    snprintf(key, sizeof(key), "web_http_path:%.*s", (int)(end - seg), seg);

    /* get exato: value é ponteiro de saída (diferente do matcher com handler) */
    const char *dir = NULL;
    gecnd_registry("get", key, (void *)&dir, NULL);
    if (!dir) return false;

    size_t n = (size_t)snprintf(out, cap, "%s", dir);
    if (n >= cap) return false;
    const char *rest = (*end == '/') ? end : "/";
    while (*rest && *rest != '?' && n < cap - 1) out[n++] = *rest++;
    out[n] = '\0';
    if (out[n - 1] == '/' && n + sizeof("index.html") < cap)
        strcpy(out + n, "index.html");
    if (strstr(out, "..")) return false; /* sem path traversal */
    return true;
}

bool web_http_path_file(const char *path, char **out_buf, size_t *out_len,
                        const char **out_mime)
{
    char fpath[512];
    if (!web_http_path_resolve(path, fpath, sizeof(fpath))) return false;

    FILE *f = fopen(fpath, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }

    char *buf = malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return false; }

    *out_buf  = buf;
    *out_len  = rd;
    if (out_mime) *out_mime = web_mime_by_ext(fpath);
    return true;
}

/* -----------------------------------------------------------------------
 * Timer callbacks — fire on next loop iteration (delay=0, never inline)
 * ---------------------------------------------------------------------- */
static void _http_fire(uv_timer_t *h)
{
    wl_conn_t *c = (wl_conn_t *)h->data;
    uv_timer_stop(h);

    gdweb_http_cb_t route_cb = web_route_http(c->path);
    if (!route_cb) {
        char  *fbuf = NULL;
        size_t flen = 0;
        if (web_http_path_file(c->path, &fbuf, &flen, NULL)) {
            if (c->on_status) c->on_status(c->id, 200, c->user);
            if (c->on_data && flen) c->on_data(c->id, fbuf, flen, c->user);
            if (c->on_done) c->on_done(c->id, c->user);
            free(fbuf);
        } else if (c->on_error) {
            c->on_error(c->id, "no route", c->user);
        }
        wl_conn_free(c);
        return;
    }

    gdweb_http_req_t req = {
        .id       = c->id,
        .method   = c->method ? c->method : "GET",
        .path     = c->path,
        .body     = c->body_len ? c->body : NULL,
        .body_len = c->body_len
    };
    route_cb(&req);
    /* response arrives via gdweb_control_server()->send() */
}

static void _ws_fire(uv_timer_t *h)
{
    wl_conn_t *c = (wl_conn_t *)h->data;
    uv_timer_stop(h);

    gdweb_ws_cb_t route_cb = web_route_ws(c->path);
    if (!route_cb) {
        if (c->on_error) c->on_error(c->id, "no route", c->user);
        wl_conn_free(c);
        return;
    }

    if (c->on_ws_open) c->on_ws_open(c->id, c->user);

    gdweb_ws_req_t req = { .id = c->id, .event = GDWEB_WS_OPEN };
    route_cb(&req);
}

/* -----------------------------------------------------------------------
 * Server control — gdweb_control_server()
 * WebLoop decides: loopback delivery or delegate to driver transport.
 * ---------------------------------------------------------------------- */
static void sv_http(gdweb_id_t id, gdweb_http_cmd_t cmd, const gdweb_value_t *value)
{
    if (id & 0x80000000u) {
        wl_conn_t *c = wl_conn_by_id(id);
        if (c && cmd == GDWEB_HTTP_STATUS && value)
            c->status = (int)value->i64;
        return;
    }
    drv_bind();
    uint32_t conn_id = req_to_conn(id);
    if (conn_id && drv.server_http) drv.server_http(conn_id, cmd, value);
}

static void sv_send(gdweb_id_t id, const char *data, size_t len)
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
    drv_bind();
    uint32_t conn_id = req_to_conn(id);
    if (conn_id && drv.server_send) drv.server_send(conn_id, data, len);
}

static void sv_send_all(const char *path, const char *data, size_t len,
                        gdweb_id_t exclude_id)
{
    drv_bind();
    uint32_t exclude_conn = (exclude_id && !(exclude_id & 0x80000000u))
                            ? req_to_conn(exclude_id) : 0;
    if (drv.server_send_all) drv.server_send_all(path, data, len, exclude_conn);
}

static void sv_start(void *loop, int port)
{
    drv_bind();
    if (drv.server_start) drv.server_start(loop, port);
}

static void sv_stop(void)
{
    drv_bind();
    if (drv.server_stop) drv.server_stop();
}

static const gdweb_server_t s_server = {
    .http     = sv_http,
    .send     = sv_send,
    .send_all = sv_send_all,
    .start    = sv_start,
    .stop     = sv_stop,
};

const gdweb_server_t *gdweb_control_server(void)
{
    return &s_server;
}

/* -----------------------------------------------------------------------
 * Loopback WS (client→server direction)
 * ---------------------------------------------------------------------- */
static void lb_ws_send(gdweb_id_t id, const char *data, size_t len)
{
    wl_conn_t *c = wl_conn_by_id(id);
    if (!c || c->type != WL_WS) return;

    gdweb_ws_cb_t route_cb = web_route_ws(c->path);
    if (!route_cb) return;

    gdweb_ws_req_t req = { .id = id, .event = GDWEB_WS_MESSAGE, .data = data, .len = len };
    route_cb(&req);
}

static void lb_ws_close(gdweb_id_t id)
{
    wl_conn_t *c = wl_conn_by_id(id);
    if (!c || c->type != WL_WS) return;

    gdweb_ws_cb_t route_cb = web_route_ws(c->path);
    if (route_cb) {
        gdweb_ws_req_t req = { .id = id, .event = GDWEB_WS_CLOSE };
        route_cb(&req);
    }
    if (c->on_ws_close) c->on_ws_close(id, c->user);
    wl_conn_free(c);
}

/* -----------------------------------------------------------------------
 * Loopback HTTP/WS request (client-originated, self://)
 * ---------------------------------------------------------------------- */
static gdweb_id_t lb_http_request(const char *path, const char *method,
                                   const char *body, size_t body_len,
                                   gdweb_status_cb_t on_status,
                                   gdweb_data_cb_t   on_data,
                                   gdweb_done_cb_t   on_done,
                                   gdweb_error_cb_t  on_error,
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

static gdweb_id_t lb_ws_connect(const char *path,
                                 gdweb_ws_open_cb_t  on_open,
                                 gdweb_ws_msg_cb_t   on_msg,
                                 gdweb_ws_close_cb_t on_close,
                                 gdweb_error_cb_t    on_error,
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
 * Client control — gdweb_control_client()
 * self:// resolve pelo loopback; o resto delega ao driver de transporte.
 * ---------------------------------------------------------------------- */
static gdweb_id_t cl_http(const char *url, gdweb_http_req_t *req,
                          gdweb_status_cb_t on_status, gdweb_data_cb_t on_data,
                          gdweb_done_cb_t on_done, gdweb_error_cb_t on_error,
                          void *user)
{
    if (!url) return 0;

    const char *method   = req && req->method   ? req->method   : "GET";
    const char *body     = req ? req->body     : NULL;
    size_t      body_len = req ? req->body_len : 0;

    if (strncmp(url, "self://", 7) == 0) {
        const char *path = url + 7;
        if (*path != '/') path--;
        gdweb_id_t id = lb_http_request(path, method, body, body_len,
                                        on_status, on_data, on_done, on_error, user);
        if (req) req->id = id;
        return id;
    }

    drv_bind();
    if (!drv.client_http) {
        if (on_error) on_error(0, "no web driver", user);
        return 0;
    }
    return drv.client_http(url, req, on_status, on_data, on_done, on_error, user);
}

static gdweb_id_t cl_ws_connect(const char *url, const char *protocol,
                                gdweb_ws_open_cb_t on_open, gdweb_ws_msg_cb_t on_msg,
                                gdweb_ws_close_cb_t on_close, gdweb_error_cb_t on_error,
                                void *user)
{
    if (!url) return 0;

    if (strncmp(url, "self://", 7) == 0) {
        const char *path = url + 7;
        if (*path != '/') path--;
        return lb_ws_connect(path, on_open, on_msg, on_close, on_error, user);
    }

    drv_bind();
    if (!drv.client_ws) {
        if (on_error) on_error(0, "no web driver", user);
        return 0;
    }
    return drv.client_ws(url, protocol, on_open, on_msg, on_close, on_error, user);
}

static void cl_send(gdweb_id_t id, const char *data, size_t len)
{
    if (id & 0x80000000u) {
        lb_ws_send(id, data, len);
        return;
    }
    drv_bind();
    if (drv.client_send) drv.client_send(id, data, len);
}

static void cl_close(gdweb_id_t id)
{
    if (id & 0x80000000u) {
        lb_ws_close(id);
        return;
    }
    drv_bind();
    if (drv.client_close) drv.client_close(id);
}

static void cl_start(void *loop)
{
    drv_bind();
    if (drv.client_start) drv.client_start(loop);
}

static void cl_stop(void)
{
    drv_bind();
    if (drv.client_stop) drv.client_stop();
}

static const gdweb_client_t s_client = {
    .http       = cl_http,
    .ws_connect = cl_ws_connect,
    .send       = cl_send,
    .close      = cl_close,
    .start      = cl_start,
    .stop       = cl_stop,
};

const gdweb_client_t *gdweb_control_client(void)
{
    return &s_client;
}

/* -----------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */
void gdweb_loop_start(void *loop)
{
    if (s_loop) return;
    s_loop = (uv_loop_t *)loop;
}

void gdweb_loop_stop(void)
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
static void register_http_functions(void)
{
    gecnd_registry("set", "function:gdweb_control_server", (void *)gdweb_control_server, NULL);
    gecnd_registry("set", "function:gdweb_control_client", (void *)gdweb_control_client, NULL);
}
