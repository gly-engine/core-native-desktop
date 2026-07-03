#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <libwebsockets.h>
#include <uv.h>

#include "gecnd.h"

extern gly_req_id_t webloop_http_request(const char *path, const char *method,
                                          const char *body, size_t body_len,
                                          gly_wc_status_cb, gly_wc_data_cb,
                                          gly_wc_done_cb, gly_wc_error_cb, void *);
extern gly_req_id_t webloop_ws_connect  (const char *path,
                                          gly_wc_ws_open_cb, gly_wc_ws_msg_cb,
                                          gly_wc_ws_close_cb, gly_wc_error_cb, void *);

#define MAX_CONNS 64
#define BUF_SIZE  8192

typedef enum { CONN_HTTP, CONN_WS } conn_type_t;

typedef struct {
    gly_req_id_t        id;
    conn_type_t        type;
    int                active;
    int                http_status;

    gly_wc_status_cb   on_status;
    gly_wc_data_cb     on_data;
    gly_wc_done_cb     on_done;
    gly_wc_error_cb    on_error;

    gly_wc_ws_open_cb  on_ws_open;
    gly_wc_ws_msg_cb   on_ws_msg;
    gly_wc_ws_close_cb on_ws_close;

    unsigned char  ws_pending[LWS_PRE + BUF_SIZE];
    size_t         ws_pending_len;
    int            ws_has_pending;

    char           http_body[BUF_SIZE];
    size_t         http_body_len;

    struct lws    *wsi;
    void          *user;
} conn_t;

static struct {
    struct lws_context *ctx;
    conn_t              pool[MAX_CONNS];
    gly_req_id_t         next_id;
    int                 started;
    char                ca_override[512];
} g;

void gamely_daemon_webclient_set_ca_path(const char *path)
{
    if (!path || !*path) { g.ca_override[0] = '\0'; return; }
    strncpy(g.ca_override, path, sizeof(g.ca_override) - 1);
    g.ca_override[sizeof(g.ca_override) - 1] = '\0';
}

static conn_t *conn_alloc(void)
{
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!g.pool[i].active) {
            memset(&g.pool[i], 0, sizeof(conn_t));
            g.pool[i].active = 1;
            if (++g.next_id == 0) g.next_id = 1;
            g.pool[i].id = g.next_id;
            return &g.pool[i];
        }
    }
    return NULL;
}

static conn_t *conn_by_id(gly_req_id_t id)
{
    for (int i = 0; i < MAX_CONNS; i++)
        if (g.pool[i].active && g.pool[i].id == id)
            return &g.pool[i];
    return NULL;
}

static void conn_free(conn_t *c)
{
    c->active = 0;
    c->wsi    = NULL;
}

static void parse_url(const char *url,
                      char *host, size_t host_sz,
                      char *path, size_t path_sz,
                      int *port, int *use_ssl)
{
    const char *p = url;
    *use_ssl = 0;
    *port    = 80;

    if      (strncmp(p, "https://", 8) == 0) { *use_ssl = 1; *port = 443; p += 8; }
    else if (strncmp(p, "http://",  7) == 0) {                *port = 80;  p += 7; }
    else if (strncmp(p, "wss://",   6) == 0) { *use_ssl = 1; *port = 443; p += 6; }
    else if (strncmp(p, "ws://",    5) == 0) {                *port = 80;  p += 5; }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t host_len;
    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else {
        host_len = slash ? (size_t)(slash - p) : strlen(p);
    }

    if (host_len >= host_sz) host_len = host_sz - 1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    if (slash)
        strncpy(path, slash, path_sz - 1);
    else {
        path[0] = '/';
        path[1] = '\0';
    }
    path[path_sz - 1] = '\0';
}

static int callback_wc(struct lws *wsi,
                       enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    conn_t *c = (conn_t *)user;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
        if (!c || !c->active) break;
        c->http_status = (int)lws_http_client_http_response(wsi);
        if (c->on_status)
            c->on_status(c->id, c->http_status, c->user);
        break;

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
        char buf[BUF_SIZE + LWS_PRE];
        char *px  = buf + LWS_PRE;
        int   lenx = BUF_SIZE;
        if (lws_http_client_read(wsi, &px, &lenx) < 0) return -1;
        return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
        if (!c || !c->active) break;
        if (c->on_data && len > 0)
            c->on_data(c->id, (const char *)in, len, c->user);
        break;

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
        if (!c || !c->active) break;
        gly_wc_done_cb  cb  = c->on_done;
        gly_req_id_t    cid = c->id;
        void           *usr = c->user;
        c->on_done  = NULL;
        c->on_error = NULL;
        c->user     = NULL;
        if (cb) cb(cid, usr);
        break;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {
        break;
    }

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
        if (!c || !c->http_body_len) break;
        unsigned char **p   = (unsigned char **)in;
        unsigned char  *end = *p + len - 1;
        char len_str[24];
        snprintf(len_str, sizeof(len_str), "%zu", c->http_body_len);
        lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH,
            (unsigned char *)len_str, (int)strlen(len_str), p, end);
        lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
            (unsigned char *)"application/json", 16, p, end);
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
        break;
    }

    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: {
        if (!c || !c->http_body_len) break;
        unsigned char buf[LWS_PRE + BUF_SIZE];
        memcpy(buf + LWS_PRE, c->http_body, c->http_body_len);
        lws_write(wsi, buf + LWS_PRE, c->http_body_len, LWS_WRITE_HTTP_FINAL);
        lws_client_http_body_pending(wsi, 0);
        break;
    }

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        if (!c) break;
        if (c->on_ws_open) c->on_ws_open(c->id, c->user);
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (!c) break;
        if (c->on_ws_msg)
            c->on_ws_msg(c->id, (const char *)in, len, c->user);
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        if (!c || !c->ws_has_pending) break;
        lws_write(wsi, &c->ws_pending[LWS_PRE], c->ws_pending_len, LWS_WRITE_TEXT);
        c->ws_has_pending = 0;
        c->ws_pending_len = 0;
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        if (!c || !c->active) break;
        const char     *msg = in ? (const char *)in : "connection error";
        gly_wc_error_cb cb  = c->on_error;
        gly_req_id_t    cid = c->id;
        void           *usr = c->user;
        c->on_error    = NULL;
        c->on_status   = NULL;
        c->on_data     = NULL;
        c->on_done     = NULL;
        c->on_ws_open  = NULL;
        c->on_ws_msg   = NULL;
        c->on_ws_close = NULL;
        c->user        = NULL;
        if (cb) cb(cid, msg, usr);
        break;
    }

    case LWS_CALLBACK_CLIENT_CLOSED: {
        if (!c || !c->active) break;
        gly_wc_ws_close_cb cb  = c->on_ws_close;
        gly_req_id_t       cid = c->id;
        void              *usr = c->user;
        int                ws  = c->type == CONN_WS;
        conn_free(c);
        if (ws && cb) cb(cid, usr);
        break;
    }

    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    { "http", callback_wc, 0, BUF_SIZE, 0, NULL, 0 },
    { "ws",   callback_wc, 0, BUF_SIZE, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

void gamely_daemon_webclient_start(void *loop)
{
    if (g.started) return;

    uv_loop_t *uv_loop = (uv_loop_t *)loop;

#if defined(GECND_HAS_SSL)
    static const char *ca_bundles[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        NULL
    };
    const char *ca_path = NULL;
    if (g.ca_override[0]) {
        if (access(g.ca_override, R_OK) == 0) {
            ca_path = g.ca_override;
        } else {
            fprintf(stderr, "[webclient] --ssl-crt nao legivel: %s\n", g.ca_override);
        }
    }
    if (!ca_path) {
        for (int i = 0; ca_bundles[i]; i++)
            if (access(ca_bundles[i], R_OK) == 0) { ca_path = ca_bundles[i]; break; }
    }
#endif
    struct lws_context_creation_info info = {0};
    info.port                  = CONTEXT_PORT_NO_LISTEN;
    info.protocols             = protocols;
    info.foreign_loops         = (void **)&uv_loop;
#if defined(GECND_HAS_SSL)
    info.options               = LWS_SERVER_OPTION_LIBUV | LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.client_ssl_ca_filepath = ca_path;
#else
    info.options               = LWS_SERVER_OPTION_LIBUV;
#endif

    g.ctx = lws_create_context(&info);
    if (!g.ctx) {
        fprintf(stderr, "[webclient] lws_create_context falhou\n");
        return;
    }

    g.started = 1;
}

void gamely_daemon_webclient_stop(void)
{
    if (!g.started) return;
    lws_context_destroy(g.ctx);
    char saved_ca[sizeof(g.ca_override)];
    memcpy(saved_ca, g.ca_override, sizeof(saved_ca));
    memset(&g, 0, sizeof(g));
    memcpy(g.ca_override, saved_ca, sizeof(g.ca_override));
}

gly_req_id_t gamely_daemon_webclient_http(
    const char      *url,
    gly_http_req_t  *req,
    gly_wc_status_cb on_status,
    gly_wc_data_cb   on_data,
    gly_wc_done_cb   on_done,
    gly_wc_error_cb  on_error,
    void            *user)
{
    if (!url) return 0;

    const char *method   = req && req->method   ? req->method   : "GET";
    const char *body     = req ? req->body     : NULL;
    size_t      body_len = req ? req->body_len : 0;

    if (strncmp(url, "self://", 7) == 0) {
        const char *path = url + 7;
        if (*path != '/') path--;
        gly_req_id_t id = webloop_http_request(path, method, body, body_len,
                                                on_status, on_data, on_done, on_error, user);
        if (req) req->id = id;
        return id;
    }

    if (!g.started) return 0;

    conn_t *c = conn_alloc();
    if (!c) { if (on_error) on_error(0, "pool cheio", user); return 0; }

    c->type      = CONN_HTTP;
    c->on_status = on_status;
    c->on_data   = on_data;
    c->on_done   = on_done;
    c->on_error  = on_error;
    c->user      = user;

    if (body && body_len) {
        size_t n = body_len < BUF_SIZE ? body_len : BUF_SIZE;
        memcpy(c->http_body, body, n);
        c->http_body_len = n;
    }

    char host[256], path[1024];
    int  port, use_ssl;
    parse_url(url, host, sizeof(host), path, sizeof(path), &port, &use_ssl);

    int ssl_flags = use_ssl ? (LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED) : 0;

    struct lws_client_connect_info ccinfo = {0};
    ccinfo.context        = g.ctx;
    ccinfo.address        = host;
    ccinfo.port           = port;
    ccinfo.path           = path;
    ccinfo.host           = host;
    ccinfo.origin         = host;
    ccinfo.protocol       = "http";
    ccinfo.ssl_connection = ssl_flags;
    ccinfo.method         = method;
    ccinfo.userdata       = c;

    c->wsi = lws_client_connect_via_info(&ccinfo);
    if (!c->wsi) {
        if (on_error) on_error(c->id, "falha ao conectar", user);
        conn_free(c);
        return 0;
    }

    if (req) req->id = c->id;
    return c->id;
}

gly_req_id_t gamely_daemon_webclient_ws_connect(
    const char        *url,
    const char        *protocol,
    gly_wc_ws_open_cb  on_open,
    gly_wc_ws_msg_cb   on_msg,
    gly_wc_ws_close_cb on_close,
    gly_wc_error_cb    on_error,
    void              *user)
{
    if (strncmp(url, "self://", 7) == 0) {
        const char *path = url + 7;
        if (*path != '/') path--;
        return webloop_ws_connect(path, on_open, on_msg, on_close, on_error, user);
    }

    if (!g.started) return 0;

    conn_t *c = conn_alloc();
    if (!c) { if (on_error) on_error(0, "pool cheio", user); return 0; }

    c->type       = CONN_WS;
    c->on_ws_open  = on_open;
    c->on_ws_msg   = on_msg;
    c->on_ws_close = on_close;
    c->on_error    = on_error;
    c->user        = user;

    char host[256], path[1024];
    int  port, use_ssl;
    parse_url(url, host, sizeof(host), path, sizeof(path), &port, &use_ssl);

    int ssl_flags = use_ssl ? (LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED) : 0;

    struct lws_client_connect_info ccinfo = {0};
    ccinfo.context        = g.ctx;
    ccinfo.address        = host;
    ccinfo.port           = port;
    ccinfo.path           = path;
    ccinfo.host           = host;
    ccinfo.origin         = host;
    ccinfo.protocol       = protocol; /* NULL = no Sec-WebSocket-Protocol header */
    ccinfo.ssl_connection = ssl_flags;
    ccinfo.userdata       = c;

    c->wsi = lws_client_connect_via_info(&ccinfo);
    if (!c->wsi) {
        if (on_error) on_error(c->id, "falha ao conectar", user);
        conn_free(c);
        return 0;
    }

    return c->id;
}

extern void webloop_client_ws_send (gly_req_id_t id, const char *data, size_t len);
extern void webloop_client_ws_close(gly_req_id_t id);

void gamely_daemon_webclient_ws_send(gly_req_id_t id, const char *data, size_t len)
{
    if (id & 0x80000000u) {
        webloop_client_ws_send(id, data, len);
        return;
    }
    conn_t *c = conn_by_id(id);
    if (!c || !c->wsi) return;
    size_t n = len < BUF_SIZE ? len : BUF_SIZE;
    memcpy(&c->ws_pending[LWS_PRE], data, n);
    c->ws_pending_len = n;
    c->ws_has_pending = 1;
    lws_callback_on_writable(c->wsi);
}

void gamely_daemon_webclient_ws_close(gly_req_id_t id)
{
    if (id & 0x80000000u) {
        webloop_client_ws_close(id);
        return;
    }
    conn_t *c = conn_by_id(id);
    if (!c || !c->wsi) return;
    lws_close_reason(c->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
    lws_set_timeout(c->wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
}

__attribute__((constructor))
static void register_webclient_functions(void) {
    gecnd_registry("set", "function:gamely_daemon_webclient_http", (void *)gamely_daemon_webclient_http, NULL);
}
