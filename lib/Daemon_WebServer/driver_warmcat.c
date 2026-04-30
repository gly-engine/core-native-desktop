#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <libwebsockets.h>
#include <khash.h>
#include <uv.h>

#include "gecnd.h"


KHASH_MAP_INIT_INT(conn_map, struct lws *)

extern gly_req_id_t webloop_alloc_req(uint32_t conn_id);
extern void         webloop_free_req (uint32_t conn_id);

#define MAX_ROUTES         64
#define MAX_WS_CLIENTS     64
#define MAX_STREAM_CLIENTS  8
#define MSG_BUF           4096
#define CHUNK_MAX         (52 * 188)  /* 9776 bytes — 52 TS packets por send    */
#define STREAM_RING       (1 << 21)   /* 2 MB por cliente                       */
#define STREAM_MASK       (STREAM_RING - 1)

/* -----------------------------------------------------------------------
 * Rotas
 * ---------------------------------------------------------------------- */
typedef enum { ROUTE_HTTP, ROUTE_WS, ROUTE_STREAM } route_type_t;

typedef struct {
    char             path[128];
    route_type_t     type;
    gly_http_cb_t    http_cb;
    gly_ws_cb_t      ws_cb;
    gly_stream_cb_t  stream_cb;
    char             content_type[64]; /* ROUTE_STREAM: Content-Type a enviar */
    char             proxy_to[128];
} route_t;

/* -----------------------------------------------------------------------
 * Sessões HTTP
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t      conn_id;
    gly_req_id_t  req_id;
    int            has_response;
    int            status;
    char           content_type[64];
    unsigned char *body;
    size_t         body_len;
    /* stream-only */
    int            is_stream;
    int            headers_sent;
    route_t       *stream_route;
    uint8_t       *ring_buf;
    unsigned int   ring_wr;
    unsigned int   ring_rd;
    int            waiting_for_idr; /* 1: overflow ocorreu — descartar até próximo IDR */
} http_session_t;

typedef struct {
    unsigned char  pending[LWS_PRE + MSG_BUF];
    size_t         pending_len;
    int            has_pending;
    route_t       *route;
    uint32_t      conn_id;
    gly_req_id_t  req_id;
    void          *usr;
} ws_session_t;

/* -----------------------------------------------------------------------
 * Singleton — single-threaded: sem lock, sem async handle
 * ---------------------------------------------------------------------- */
static struct {
    struct lws_context  *ctx;
    struct lws_protocols protocols[3];

    route_t              routes[MAX_ROUTES];
    int                  route_count;

    khash_t(conn_map)   *conn_map;

    uint32_t             ws_ids[MAX_WS_CLIENTS];
    ws_session_t        *ws_sessions[MAX_WS_CLIENTS];
    int                  ws_count;

    uint32_t             stream_ids[MAX_STREAM_CLIENTS];
    http_session_t      *stream_sessions[MAX_STREAM_CLIENTS];
    int                  stream_count;

    uint32_t             next_id;
    int                  started;
} g;

/* -----------------------------------------------------------------------
 * Ring buffer helpers — sem lock (mesma thread do loop)
 * ---------------------------------------------------------------------- */

/* Detecta se o bloco TS contém um pacote PAT (PID=0, PUSI=1).
 * PAT precede todo IDR quando mpegts_flags=resend_headers está ativo.
 * Critério: byte0=0x47, byte1 & 0x5f == 0x40, byte2 == 0x00            */
static int ts_has_pat(const uint8_t *data, int size)
{
    for (int i = 0; i + 3 <= size; i += 188)
        if (data[i] == 0x47 && (data[i+1] & 0x5fu) == 0x40u && data[i+2] == 0x00u)
            return 1;
    return 0;
}

static void ring_write(http_session_t *s, const uint8_t *data, int size)
{
    unsigned int avail = STREAM_RING - (s->ring_wr - s->ring_rd);

    /* overflow: não cabe — aguardar até próximo IDR antes de gravar */
    if ((unsigned int)size > avail)
        s->waiting_for_idr = 1;

    if (s->waiting_for_idr) {
        /* descarta frames até receber PAT+PMT (início de IDR) */
        if (!ts_has_pat(data, size)) return;

        /* IDR chegou: recalcula espaço disponível e faz room se necessário */
        avail = STREAM_RING - (s->ring_wr - s->ring_rd);
        if ((unsigned int)size > avail) {
            unsigned int new_rd = s->ring_wr - STREAM_RING + (unsigned int)size;
            s->ring_rd = ((new_rd + 187u) / 188u) * 188u;
        }
        s->waiting_for_idr = 0;
    }

    unsigned int off   = s->ring_wr & STREAM_MASK;
    unsigned int part1 = STREAM_RING - off;
    if ((unsigned int)size <= part1) {
        memcpy(s->ring_buf + off, data, (size_t)size);
    } else {
        memcpy(s->ring_buf + off, data, part1);
        memcpy(s->ring_buf, data + part1, (size_t)(size - (int)part1));
    }
    s->ring_wr += (unsigned int)size;
}

static int ring_read(http_session_t *s, uint8_t *dst, int max)
{
    unsigned int avail = s->ring_wr - s->ring_rd;
    if (avail < 188) return 0;

    /* garante múltiplo de 188 */
    unsigned int n = avail < (unsigned int)max ? avail : (unsigned int)max;
    n = (n / 188) * 188;
    if (!n) return 0;

    unsigned int off   = s->ring_rd & STREAM_MASK;
    unsigned int part1 = STREAM_RING - off;
    if (n <= part1) {
        memcpy(dst, s->ring_buf + off, n);
    } else {
        memcpy(dst, s->ring_buf + off, part1);
        memcpy(dst + part1, s->ring_buf, n - part1);
    }
    s->ring_rd += n;
    return (int)n;
}

/* -----------------------------------------------------------------------
 * Rotas — busca binária
 * ---------------------------------------------------------------------- */
static int route_cmp_key(const void *key, const void *elem)
{
    const route_t *k = (const route_t *)key;
    const route_t *e = (const route_t *)elem;
    if (k->type != e->type) return (int)k->type - (int)e->type;
    return strcmp(k->path, e->path);
}

static route_t *find_route(const char *path, route_type_t type)
{
    route_t key;
    memset(&key, 0, sizeof(key));
    key.type = type;
    strncpy(key.path, path, sizeof(key.path) - 1);
    return (route_t *)bsearch(&key, g.routes, (size_t)g.route_count,
                               sizeof(route_t), route_cmp_key);
}

static route_t *insert_route(const char *path, route_type_t type)
{
    if (g.route_count >= MAX_ROUTES) {
        fprintf(stderr, "[webserver] MAX_ROUTES atingido\n");
        return NULL;
    }
    int pos = g.route_count;
    for (int i = 0; i < g.route_count; i++) {
        int c = (int)type - (int)g.routes[i].type;
        if (c == 0) c = strcmp(path, g.routes[i].path);
        if (c < 0) { pos = i; break; }
        if (c == 0) return &g.routes[i];
    }
    memmove(&g.routes[pos + 1], &g.routes[pos],
            sizeof(route_t) * (size_t)(g.route_count - pos));
    g.route_count++;
    memset(&g.routes[pos], 0, sizeof(route_t));
    strncpy(g.routes[pos].path, path, sizeof(g.routes[pos].path) - 1);
    g.routes[pos].type = type;
    return &g.routes[pos];
}

static route_t *resolve_route(route_t *r)
{
    uint64_t visited = 0;
    while (r && r->proxy_to[0]) {
        ptrdiff_t idx = r - g.routes;
        if (idx < 0 || idx >= MAX_ROUTES) break;
        uint64_t bit = (uint64_t)1 << idx;
        if (visited & bit) {
            fprintf(stderr, "[webserver] ciclo detectado em proxy '%s'\n", r->path);
            return NULL;
        }
        visited |= bit;
        r = find_route(r->proxy_to, r->type);
    }
    return r;
}

/* -----------------------------------------------------------------------
 * conn_map helpers
 * ---------------------------------------------------------------------- */
static uint32_t alloc_id(void)
{
    if (++g.next_id == 0) g.next_id = 1;
    return g.next_id;
}

static struct lws *wsi_by_conn_id(uint32_t id)
{
    if (!id) return NULL;
    khint_t k = kh_get(conn_map, g.conn_map, id);
    if (k == kh_end(g.conn_map)) return NULL;
    return kh_val(g.conn_map, k);
}

static void conn_register(uint32_t id, struct lws *wsi)
{
    int ret;
    khint_t k = kh_put(conn_map, g.conn_map, id, &ret);
    assert(ret >= 0);
    kh_val(g.conn_map, k) = wsi;
}

static void conn_remove(uint32_t id)
{
    khint_t k = kh_get(conn_map, g.conn_map, id);
    if (k != kh_end(g.conn_map)) kh_del(conn_map, g.conn_map, k);
}

/* -----------------------------------------------------------------------
 * _ws_send_wsi
 * ---------------------------------------------------------------------- */
static void _ws_send_wsi(struct lws *wsi, const char *text, size_t len)
{
    ws_session_t *s = (ws_session_t *)lws_wsi_user(wsi);
    if (!s) return;
    size_t n = len < (MSG_BUF - 1) ? len : (MSG_BUF - 1);
    memcpy(&s->pending[LWS_PRE], text, n);
    s->pending_len = n;
    s->has_pending = 1;
    lws_callback_on_writable(wsi);
}

/* -----------------------------------------------------------------------
 * callback_http
 * ---------------------------------------------------------------------- */
static int callback_http(struct lws *wsi,
                         enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    http_session_t *s = (http_session_t *)user;
    (void)len;

    switch (reason) {

    case LWS_CALLBACK_HTTP: {
        if (!s) return -1;
        if (lws_hdr_total_length(wsi, WSI_TOKEN_UPGRADE) > 0)
            return lws_callback_http_dummy(wsi, reason, user, in, len);

        s->conn_id = alloc_id();
        conn_register(s->conn_id, wsi);
        s->req_id = webloop_alloc_req(s->conn_id);

        const char *path = in ? (const char *)in : "/";

        /* append query string — LWS splits URI and args into separate tokens */
        char full_path[512];
        {
            int qlen = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
            if (qlen > 0) {
                char qbuf[384] = {0};
                lws_hdr_copy(wsi, qbuf, sizeof(qbuf), WSI_TOKEN_HTTP_URI_ARGS);
                snprintf(full_path, sizeof(full_path), "%s?%s", path, qbuf);
            } else {
                snprintf(full_path, sizeof(full_path), "%s", path);
            }
        }

        /* verifica rota stream */
        route_t *r = find_route(path, ROUTE_STREAM);
        if (r) {
            route_t *real = resolve_route(r);
            if (real && real->type == ROUTE_STREAM) {
                uint8_t *buf = malloc(STREAM_RING);
                if (!buf) { webloop_free_req(s->conn_id); conn_remove(s->conn_id); return -1; }

                s->is_stream        = 1;
                s->headers_sent     = 0;
                s->stream_route     = real;
                s->ring_buf         = buf;
                s->ring_wr          = 0;
                s->ring_rd          = 0;
                s->waiting_for_idr  = 0;

                if (g.stream_count < MAX_STREAM_CLIENTS) {
                    g.stream_ids[g.stream_count]      = s->conn_id;
                    g.stream_sessions[g.stream_count] = s;
                    g.stream_count++;
                } else {
                    free(buf);
                    s->ring_buf = NULL;
                    fprintf(stderr, "[webserver] MAX_STREAM_CLIENTS atingido\n");
                    webloop_free_req(s->conn_id);
                    conn_remove(s->conn_id);
                    return -1;
                }

                /* desabilita o PENDING_TIMEOUT_HTTP_CONTENT (padrão 15 s do LWS)
                 * que encerraria streams chunked que nunca têm fim natural */
                lws_set_timeout(wsi, NO_PENDING_TIMEOUT, 0);

                /* notifica o serviço — pode já escrever IDR cache no ring */
                if (real->stream_cb)
                    real->stream_cb(s->req_id, true);

                /* agenda envio imediato de headers (+ IDR cache se já no ring) */
                lws_callback_on_writable(wsi);
                return 0;
            }
        }

        /* rota HTTP normal */
        r = resolve_route(find_route(path, ROUTE_HTTP));
        if (r && r->http_cb) {
            const char *method = "GET";
            char *p_uri;
            int l_uri;
            int m = lws_http_get_uri_and_method(wsi, &p_uri, &l_uri);
            switch (m) {
                case LWSHUMETH_GET:     method = "GET";     break;
                case LWSHUMETH_POST:    method = "POST";    break;
                case LWSHUMETH_PUT:     method = "PUT";     break;
                case LWSHUMETH_DELETE:  method = "DELETE";  break;
                case LWSHUMETH_PATCH:   method = "PATCH";   break;
                case LWSHUMETH_OPTIONS: method = "OPTIONS"; break;
                case LWSHUMETH_HEAD:    method = "HEAD";    break;
                case LWSHUMETH_CONNECT: method = "CONNECT"; break;
            }
            gly_http_req_t req = { .id = s->req_id, .path = full_path, .method = method };
            r->http_cb(&req);
        } else {
            s->status = 404;
            strncpy(s->content_type, "text/plain", sizeof(s->content_type) - 1);
            static const char not_found[] = "Not Found";
            s->body = malloc(sizeof(not_found) - 1);
            if (s->body) {
                memcpy(s->body, not_found, sizeof(not_found) - 1);
                s->body_len = sizeof(not_found) - 1;
            }
            s->has_response = 1;
        }
        lws_callback_on_writable(wsi);
        return 0;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
        if (!s) break;

        /* --- streaming --- */
        if (s->is_stream) {
            if (!s->headers_sent) {
                const char *ct = (s->stream_route && s->stream_route->content_type[0])
                                     ? s->stream_route->content_type
                                     : "video/mp2t";
                unsigned char hdr[512], *p = hdr, *end = hdr + sizeof(hdr);
                if (lws_add_http_header_status(wsi, 200, &p, end) ||
                    lws_add_http_header_by_name(wsi,
                        (const unsigned char *)"content-type:",
                        (const unsigned char *)ct, (int)strlen(ct), &p, end) ||
                    lws_add_http_header_by_name(wsi,
                        (const unsigned char *)"transfer-encoding:",
                        (const unsigned char *)"chunked", 7, &p, end) ||
                    lws_add_http_header_by_name(wsi,
                        (const unsigned char *)"cache-control:",
                        (const unsigned char *)"no-cache, no-store", 18, &p, end) ||
                    lws_add_http_header_by_name(wsi,
                        (const unsigned char *)"connection:",
                        (const unsigned char *)"keep-alive", 10, &p, end) ||
                    lws_finalize_http_header(wsi, &p, end) ||
                    lws_write(wsi, hdr, (size_t)(p - hdr),
                              LWS_WRITE_HTTP_HEADERS) < 0)
                    return -1;
                s->headers_sent = 1;
                /* re-arma WRITEABLE para drenar o ring (IDR cache já pode estar lá) */
                lws_callback_on_writable(wsi);
                return 0;
            }

            /* drena ring em chunks de múltiplos de 188
             * formato chunked manual: "hex\r\n<dados>\r\n" */
            unsigned char wbuf[LWS_PRE + 12 + CHUNK_MAX + 2];
            unsigned char *chunk    = wbuf + LWS_PRE;
            unsigned char *data_dst = chunk + 12;

            int n    = ring_read(s, data_dst, CHUNK_MAX);
            int more = (int)(s->ring_wr - s->ring_rd);

            if (n > 0) {
                int hlen = snprintf((char *)chunk, 12, "%x\r\n", (unsigned)n);
                memmove(chunk + hlen, data_dst, (size_t)n);
                chunk[hlen + n]     = '\r';
                chunk[hlen + n + 1] = '\n';
                int total = hlen + n + 2;
                if (lws_write(wsi, chunk, (size_t)total, LWS_WRITE_HTTP) < total)
                    return -1;
                if (more >= 188)
                    lws_callback_on_writable(wsi);
            }
            return 0;
        }

        /* --- HTTP normal --- */
        if (!s->has_response) break;

        unsigned char hdr[512], *p = hdr, *end = hdr + sizeof(hdr);
        if (lws_add_http_header_status(wsi, (unsigned int)s->status, &p, end) ||
            lws_add_http_header_by_token(wsi,
                WSI_TOKEN_HTTP_CONTENT_TYPE,
                (unsigned char *)s->content_type,
                (int)strlen(s->content_type), &p, end) ||
            lws_add_http_header_content_length(wsi, s->body_len, &p, end) ||
            lws_finalize_http_header(wsi, &p, end))
            return -1;

        if (lws_write(wsi, hdr, (size_t)(p - hdr), LWS_WRITE_HTTP_HEADERS) < 0)
            return -1;
        if (s->body && s->body_len) {
            if (lws_write(wsi, s->body, s->body_len, LWS_WRITE_HTTP_FINAL) < 0)
                return -1;
        } else {
            unsigned char empty[LWS_PRE + 1] = {0};
            if (lws_write(wsi, empty + LWS_PRE, 0, LWS_WRITE_HTTP_FINAL) < 0)
                return -1;
        }

        s->has_response = 0;
        free(s->body);
        s->body = NULL;
        s->body_len = 0;
        conn_remove(s->conn_id);
        lws_http_transaction_completed(wsi);
        return -1;
    }

    case LWS_CALLBACK_CLOSED_HTTP:
        if (!s || !s->conn_id) break;
        free(s->body);
        s->body = NULL;
        if (s->is_stream) {
            for (int i = 0; i < g.stream_count; i++) {
                if (g.stream_ids[i] != s->conn_id) continue;
                g.stream_ids[i]      = g.stream_ids[--g.stream_count];
                g.stream_sessions[i] = g.stream_sessions[g.stream_count];
                g.stream_ids[g.stream_count]      = 0;
                g.stream_sessions[g.stream_count] = NULL;
                break;
            }
            uint8_t *to_free = s->ring_buf;
            s->ring_buf = NULL;
            free(to_free);

            route_t *real = resolve_route(s->stream_route);
            if (real && real->stream_cb)
                real->stream_cb(s->req_id, false);
        }
        webloop_free_req(s->conn_id);
        conn_remove(s->conn_id);
        break;

    default:
        break;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

/* -----------------------------------------------------------------------
 * callback_ws
 * ---------------------------------------------------------------------- */
static int callback_ws(struct lws *wsi,
                       enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    ws_session_t *s = (ws_session_t *)user;
    if (!s) return 0;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED: {
        char path[128] = "/";
        lws_hdr_copy(wsi, path, sizeof(path), WSI_TOKEN_GET_URI);
        s->route   = find_route(path, ROUTE_WS);
        s->conn_id = alloc_id();
        conn_register(s->conn_id, wsi);
        s->req_id  = webloop_alloc_req(s->conn_id);

        if (g.ws_count < MAX_WS_CLIENTS) {
            g.ws_ids[g.ws_count]      = s->conn_id;
            g.ws_sessions[g.ws_count] = s;
            g.ws_count++;
        } else {
            fprintf(stderr, "[webserver] MAX_WS_CLIENTS atingido, rejeitando\n");
            webloop_free_req(s->conn_id);
            conn_remove(s->conn_id);
            return -1;
        }

        /* extrair query string — LWS separa URI e args em tokens distintos */
        char qbuf[384] = {0};
        int  qlen      = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
        if (qlen > 0)
            lws_hdr_copy(wsi, qbuf, sizeof(qbuf), WSI_TOKEN_HTTP_URI_ARGS);

        route_t *real = resolve_route(s->route);
        if (real && real->ws_cb) {
            gly_ws_req_t req = {
                .id    = s->req_id,
                .event = GLY_WS_OPEN,
                .data  = (qlen > 0) ? qbuf : NULL,
                .len   = (qlen > 0) ? strlen(qbuf) : 0,
                .usr   = &s->usr
            };
            real->ws_cb(&req);
        }
        break;
    }

    case LWS_CALLBACK_CLOSED: {
        webloop_free_req(s->conn_id);
        conn_remove(s->conn_id);
        for (int i = 0; i < g.ws_count; i++) {
            if (g.ws_ids[i] == s->conn_id) {
                g.ws_ids[i]               = g.ws_ids[--g.ws_count];
                g.ws_sessions[i]          = g.ws_sessions[g.ws_count];
                g.ws_ids[g.ws_count]      = 0;
                g.ws_sessions[g.ws_count] = NULL;
                break;
            }
        }
        route_t *real = resolve_route(s->route);
        if (real && real->ws_cb) {
            gly_ws_req_t req = { .id=s->req_id, .event=GLY_WS_CLOSE, .usr=&s->usr };
            real->ws_cb(&req);
        }
        memset(s, 0, sizeof(*s));
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        route_t *real = resolve_route(s->route);
        if (real && real->ws_cb) {
            gly_ws_req_t req = {
                .id    = s->req_id,
                .event = GLY_WS_MESSAGE,
                .data  = (const char *)in,
                .len   = len,
                .usr   = &s->usr
            };
            real->ws_cb(&req);
        }
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE:
        if (s->has_pending) {
            lws_write(wsi, &s->pending[LWS_PRE], s->pending_len, LWS_WRITE_TEXT);
            s->has_pending = 0;
            s->pending_len = 0;
        }
        break;

    default:
        break;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * API pública — registro de rotas
 * ---------------------------------------------------------------------- */
void gamely_daemon_webserver_route_http(const char *path, gly_http_cb_t cb)
{
    route_t *r = insert_route(path, ROUTE_HTTP);
    if (!r) return;
    r->http_cb = cb;
}

void gamely_daemon_webserver_route_ws(const char *path, gly_ws_cb_t cb)
{
    route_t *r = insert_route(path, ROUTE_WS);
    if (!r) return;
    r->ws_cb = cb;
}

void gamely_daemon_webserver_route_stream(const char *path,
                                          const char *content_type,
                                          gly_stream_cb_t cb)
{
    route_t *r = insert_route(path, ROUTE_STREAM);
    if (!r) return;
    r->stream_cb = cb;
    strncpy(r->content_type,
            (content_type && content_type[0]) ? content_type : "video/mp2t",
            sizeof(r->content_type) - 1);
}

void gamely_daemon_webserver_start(void *loop, int port)
{
    if (!loop || !port || g.started) return;
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

    g.conn_map = kh_init(conn_map);
    if (!g.conn_map) {
        fprintf(stderr, "[webserver] falha ao alocar conn_map\n");
        abort();
    }

    g.protocols[0] = (struct lws_protocols){
        "http", callback_http, sizeof(http_session_t), 0, 0, NULL, 0
    };
    g.protocols[1] = (struct lws_protocols){
        "ws", callback_ws, sizeof(ws_session_t), MSG_BUF, 0, NULL, 0
    };
    g.protocols[2] = (struct lws_protocols){ NULL, NULL, 0, 0, 0, NULL, 0 };

    uv_loop_t *uv_loop = (uv_loop_t *)loop;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port          = port;
    info.protocols     = g.protocols;
    info.iface         = NULL;
    info.foreign_loops = (void **)&uv_loop;
    info.options       = LWS_SERVER_OPTION_LIBUV;

    g.ctx = lws_create_context(&info);
    if (!g.ctx) {
        fprintf(stderr,
            "[webserver] ERRO: lws_create_context falhou.\n"
            "            Compile o lws com -DLWS_WITH_LIBUV=ON\n");
        kh_destroy(conn_map, g.conn_map);
        g.conn_map = NULL;
        abort();
    }

    g.started = 1;
    printf("[webserver] ouvindo 0.0.0.0:%d  (libuv foreign loop)\n", port);
}

void gamely_daemon_webserver_stop(void)
{
    if (!g.started) return;

    if (g.ctx) {
        lws_context_destroy(g.ctx);
        g.ctx = NULL;
    }
    if (g.conn_map) {
        kh_destroy(conn_map, g.conn_map);
        g.conn_map = NULL;
    }

    memset(&g, 0, sizeof(g));
}

/* -----------------------------------------------------------------------
 * driver_http_send / driver_ws_send / driver_ws_send_all / driver_stream_write
 * Internal transport functions — called by WebLoop (service_loopback.c) only.
 * ---------------------------------------------------------------------- */
void driver_http_send(uint32_t conn_id, int status, const char *content_type,
                      const char *body, size_t body_len)
{
    if (!g.started) return;
    struct lws *wsi = wsi_by_conn_id(conn_id);
    if (!wsi) return;
    http_session_t *s = (http_session_t *)lws_wsi_user(wsi);
    if (!s) return;

    s->status = status;
    strncpy(s->content_type, content_type ? content_type : "text/plain",
            sizeof(s->content_type) - 1);
    free(s->body);
    s->body     = NULL;
    s->body_len = 0;
    if (body && body_len) {
        s->body = malloc(body_len);
        if (s->body) {
            memcpy(s->body, body, body_len);
            s->body_len = body_len;
        }
    }
    s->has_response = 1;
    lws_callback_on_writable(wsi);
}

void driver_ws_send(uint32_t conn_id, const char *data, size_t len)
{
    if (!g.started || !data || !len) return;
    struct lws *wsi = wsi_by_conn_id(conn_id);
    if (wsi) _ws_send_wsi(wsi, data, len);
}

void driver_ws_send_all(const char *path, const char *data, size_t len,
                        uint32_t exclude_conn_id)
{
    if (!g.started || !data || !len) return;
    for (int i = 0; i < g.ws_count; i++) {
        if (g.ws_ids[i] == exclude_conn_id) continue;
        if (path) {
            ws_session_t *s = g.ws_sessions[i];
            if (!s || !s->route) continue;
            const char *rpath = s->route->path;
            const char *tpath = s->route->proxy_to[0] ? s->route->proxy_to : rpath;
            if (strcmp(rpath, path) != 0 && strcmp(tpath, path) != 0) continue;
        }
        struct lws *wsi = wsi_by_conn_id(g.ws_ids[i]);
        if (wsi) _ws_send_wsi(wsi, data, len);
    }
}

void driver_stream_write(uint32_t conn_id, const uint8_t *buf, int size)
{
    if (!g.started || size <= 0 || !conn_id) return;
    for (int i = 0; i < g.stream_count; i++) {
        if (g.stream_ids[i] != conn_id) continue;
        http_session_t *s = g.stream_sessions[i];
        if (s && s->ring_buf) {
            ring_write(s, buf, size);
            struct lws *wsi = wsi_by_conn_id(conn_id);
            if (wsi) lws_callback_on_writable(wsi);
        }
        return;
    }
}
