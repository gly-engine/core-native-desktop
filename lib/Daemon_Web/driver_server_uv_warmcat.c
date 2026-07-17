#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>

#include <libwebsockets.h>
#include <khash.h>
#include <uv.h>

#include "gecnd.h"
#include "gdweb.h"


KHASH_MAP_INIT_INT(conn_map, struct lws *)

extern gdweb_id_t    web_alloc_req   (uint32_t conn_id);
extern void            web_free_req    (uint32_t conn_id);
extern gdweb_http_cb_t   web_route_http  (const char *path);
extern gdweb_ws_cb_t     web_route_ws    (const char *path);
extern gdweb_stream_cb_t web_route_stream(const char *path);
extern bool              web_http_path_file(const char *path, char **out_buf,
                                            size_t *out_len, const char **out_mime);

#define MAX_WS_CLIENTS     64
#define MAX_STREAM_CLIENTS  8
#define MSG_BUF           4096

/* validity/keepalive: lws manda PING apos N s sem trafego e derruba a
 * conexao ws se nem o PONG chegar — detecta peer morto (cabo, kill -9)
 * sem esperar o default de ~5 min do lws. So afeta conexoes ws; h1 nao
 * arma validity. */
static const lws_retry_bo_t s_retry_policy = {
    .secs_since_valid_ping   = 15,
    .secs_since_valid_hangup = 30,
};
#define CHUNK_MAX         (52 * 188)  /* 9776 bytes — 52 TS packets por send    */
#define STREAM_RING       (1 << 21)   /* 2 MB por cliente                       */
#define STREAM_MASK       (STREAM_RING - 1)

/* -----------------------------------------------------------------------
 * Sessões HTTP
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t      conn_id;
    gdweb_id_t  req_id;
    int            has_response;
    int            status;          /* 0 = 200 */
    char           content_type[64]; /* vazio = default por tipo de resposta */
    unsigned char *body;
    size_t         body_len;
    /* stream-only */
    int             is_stream;
    int             headers_sent;
    gdweb_stream_cb_t stream_cb;
    uint8_t        *ring_buf;
    unsigned int    ring_wr;
    unsigned int    ring_rd;
    int             waiting_for_idr; /* 1: overflow ocorreu — descartar até próximo IDR */
} http_session_t;

typedef struct {
    unsigned char  pending[LWS_PRE + MSG_BUF];
    size_t         pending_len;
    int            has_pending;
    int            close_pending; /* servico pediu close: fecha apos drenar */
    gdweb_ws_cb_t    cb;
    char           path[128];
    uint32_t      conn_id;
    gdweb_id_t  req_id;
    void          *usr;
} ws_session_t;

/* -----------------------------------------------------------------------
 * Singleton — single-threaded: sem lock, sem async handle
 * ---------------------------------------------------------------------- */
static struct {
    struct lws_context  *ctx;
    struct lws_protocols protocols[3];

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
 * CORS — reflete o Origin da requisição (aceita qualquer origem)
 * ---------------------------------------------------------------------- */
static int add_cors_headers(struct lws *wsi, unsigned char **p, unsigned char *end)
{
    char origin[256];
    int  olen = lws_hdr_copy(wsi, origin, sizeof(origin), WSI_TOKEN_ORIGIN);

    /* sem Origin (requisição não-CORS): libera para qualquer um */
    const char *allow = (olen > 0) ? origin : "*";

    if (lws_add_http_header_by_name(wsi,
            (const unsigned char *)"access-control-allow-origin:",
            (const unsigned char *)allow, (int)strlen(allow), p, end))
        return -1;
    if (lws_add_http_header_by_name(wsi,
            (const unsigned char *)"access-control-allow-credentials:",
            (const unsigned char *)"true", 4, p, end))
        return -1;
    if (lws_add_http_header_by_name(wsi,
            (const unsigned char *)"access-control-allow-methods:",
            (const unsigned char *)"GET, POST, PUT, DELETE, PATCH, OPTIONS", 38, p, end))
        return -1;
    if (lws_add_http_header_by_name(wsi,
            (const unsigned char *)"access-control-allow-headers:",
            (const unsigned char *)"Content-Type, Authorization", 27, p, end))
        return -1;
    return 0;
}

/* -----------------------------------------------------------------------
 * session_stream_upgrade — converte a conexão HTTP em stream chunked
 * ---------------------------------------------------------------------- */
static int session_stream_upgrade(struct lws *wsi, http_session_t *s,
                                  gdweb_stream_cb_t cb)
{
    if (s->is_stream) return 0;
    if (g.stream_count >= MAX_STREAM_CLIENTS) {
        fprintf(stderr, "[webserver] MAX_STREAM_CLIENTS atingido\n");
        return -1;
    }
    uint8_t *buf = malloc(STREAM_RING);
    if (!buf) return -1;

    s->is_stream       = 1;
    s->headers_sent    = 0;
    s->stream_cb       = cb;
    s->ring_buf        = buf;
    s->ring_wr         = 0;
    s->ring_rd         = 0;
    s->waiting_for_idr = 0;

    g.stream_ids[g.stream_count]      = s->conn_id;
    g.stream_sessions[g.stream_count] = s;
    g.stream_count++;

    /* desabilita o PENDING_TIMEOUT_HTTP_CONTENT (padrão 15 s do LWS)
     * que encerraria streams chunked que nunca têm fim natural */
    lws_set_timeout(wsi, NO_PENDING_TIMEOUT, 0);

    /* notifica o serviço — pode já escrever IDR cache no ring */
    if (cb) cb(s->req_id, true);

    /* agenda envio imediato de headers (+ IDR cache se já no ring) */
    lws_callback_on_writable(wsi);
    return 0;
}

/* -----------------------------------------------------------------------
 * ws_upgrade_confirm — upgrade websocket para topico sem rota: responde 404
 * em vez de aceitar uma conexao surda (browser recebe onerror).
 * O CONFIRM_UPGRADE chega no protocolo default do vhost (que o pvo abaixo
 * troca para "ws"), entao os dois callbacks delegam para ca.
 * ---------------------------------------------------------------------- */
static int ws_upgrade_confirm(struct lws *wsi, void *in)
{
    if (in && !strcasecmp((const char *)in, "websocket")) {
        char path[128] = "/";
        lws_hdr_copy(wsi, path, sizeof(path), WSI_TOKEN_GET_URI);
        if (!web_route_ws(path)) {
            lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND,
                                   "ws route not found");
            return 1; /* >0: resposta ja emitida, lws completa a transacao */
        }
    }
    return 0;
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

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
        return ws_upgrade_confirm(wsi, in);

    case LWS_CALLBACK_HTTP: {
        if (!s) return -1;
        if (lws_hdr_total_length(wsi, WSI_TOKEN_UPGRADE) > 0)
            return lws_callback_http_dummy(wsi, reason, user, in, len);

        s->conn_id = alloc_id();
        conn_register(s->conn_id, wsi);
        s->req_id = web_alloc_req(s->conn_id);

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
        gdweb_stream_cb_t stream_cb = web_route_stream(path);
        if (stream_cb) {
            if (session_stream_upgrade(wsi, s, stream_cb) != 0) {
                web_free_req(s->conn_id);
                conn_remove(s->conn_id);
                return -1;
            }
            return 0;
        }

        /* rota HTTP normal */
        gdweb_http_cb_t http_cb = web_route_http(path);
        if (http_cb) {
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
            gdweb_http_req_t req = { .id = s->req_id, .path = full_path, .method = method };
            http_cb(&req);
        } else {
            char       *fbuf = NULL;
            size_t      flen = 0;
            const char *mime = NULL;
            if (web_http_path_file(path, &fbuf, &flen, &mime)) {
                s->status = 200;
                strncpy(s->content_type, mime, sizeof(s->content_type) - 1);
                s->body     = fbuf;
                s->body_len = flen;
            } else {
                s->status = 404;
                strncpy(s->content_type, "text/plain", sizeof(s->content_type) - 1);
                static const char not_found[] = "Not Found";
                s->body = malloc(sizeof(not_found) - 1);
                if (s->body) {
                    memcpy(s->body, not_found, sizeof(not_found) - 1);
                    s->body_len = sizeof(not_found) - 1;
                }
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
                const char *ct = s->content_type[0] ? s->content_type
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
                    add_cors_headers(wsi, &p, end) ||
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

        const char *ct = s->content_type[0] ? s->content_type : "text/plain";
        unsigned char hdr[512], *p = hdr, *end = hdr + sizeof(hdr);
        if (lws_add_http_header_status(wsi, s->status ? (unsigned int)s->status : 200u, &p, end) ||
            lws_add_http_header_by_token(wsi,
                WSI_TOKEN_HTTP_CONTENT_TYPE,
                (unsigned char *)ct,
                (int)strlen(ct), &p, end) ||
            lws_add_http_header_content_length(wsi, s->body_len, &p, end) ||
            add_cors_headers(wsi, &p, end) ||
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

            if (s->stream_cb)
                s->stream_cb(s->req_id, false);
        }
        web_free_req(s->conn_id);
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
    /* pre-upgrade: com o pvo "default" o wsi h1 nasce vinculado a este
     * protocolo, entao a validacao de rota chega aqui e nao no http */
    if (reason == LWS_CALLBACK_HTTP_CONFIRM_UPGRADE)
        return ws_upgrade_confirm(wsi, in);

    ws_session_t *s = (ws_session_t *)user;
    if (!s) return 0;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED: {
        char path[128] = "/";
        lws_hdr_copy(wsi, path, sizeof(path), WSI_TOKEN_GET_URI);
        gdweb_ws_cb_t cb = web_route_ws(path);

        /* sem rota: CONFIRM_UPGRADE ja barra com 404; guarda extra para
         * rota removida entre o handshake e o established. s->cb fica
         * NULL, entao o CLOSED que segue nao notifica servico nenhum. */
        if (!cb) return -1;

        if (g.ws_count >= MAX_WS_CLIENTS) {
            fprintf(stderr, "[webserver] MAX_WS_CLIENTS atingido, rejeitando\n");
            return -1;
        }

        s->cb = cb;
        strncpy(s->path, path, sizeof(s->path) - 1);
        s->conn_id = alloc_id();
        conn_register(s->conn_id, wsi);
        s->req_id  = web_alloc_req(s->conn_id);

        g.ws_ids[g.ws_count]      = s->conn_id;
        g.ws_sessions[g.ws_count] = s;
        g.ws_count++;

        /* extrair query string — LWS separa URI e args em tokens distintos */
        char qbuf[384] = {0};
        int  qlen      = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
        if (qlen > 0)
            lws_hdr_copy(wsi, qbuf, sizeof(qbuf), WSI_TOKEN_HTTP_URI_ARGS);

        if (s->cb) {
            gdweb_ws_req_t req = {
                .id    = s->req_id,
                .event = GDWEB_WS_OPEN,
                .data  = (qlen > 0) ? qbuf : NULL,
                .len   = (qlen > 0) ? strlen(qbuf) : 0,
                .usr   = &s->usr
            };
            s->cb(&req);
        }
        break;
    }

    case LWS_CALLBACK_CLOSED: {
        if (!s->conn_id) break; /* conexao rejeitada no established */
        web_free_req(s->conn_id);
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
        if (s->cb) {
            gdweb_ws_req_t req = { .id=s->req_id, .event=GDWEB_WS_CLOSE, .usr=&s->usr };
            s->cb(&req);
        }
        memset(s, 0, sizeof(*s));
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        if (s->cb) {
            gdweb_ws_req_t req = {
                .id    = s->req_id,
                .event = GDWEB_WS_MESSAGE,
                .data  = (const char *)in,
                .len   = len,
                .usr   = &s->usr
            };
            s->cb(&req);
        }
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE:
        if (s->has_pending) {
            lws_write(wsi, &s->pending[LWS_PRE], s->pending_len, LWS_WRITE_TEXT);
            s->has_pending = 0;
            s->pending_len = 0;
            /* drena o frame pendente antes de fechar */
            if (s->close_pending) lws_callback_on_writable(wsi);
            break;
        }
        if (s->close_pending) {
            lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
            return -1; /* LWS_CALLBACK_CLOSED notifica o servico */
        }
        break;

    default:
        break;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Lifecycle — expostos via gdweb_control_server() (service_loopback.c)
 * ---------------------------------------------------------------------- */
static void driver_server_start(void *loop, int port)
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

    /* upgrade ws sem header Sec-WebSocket-Protocol: lws vincula ao
     * default_protocol_index do vhost (protocols[0] = "http", que ignora
     * os eventos ws). Este pvo marca "ws" como default para que clientes
     * sem subprotocolo tambem cheguem ao callback_ws. */
    static const struct lws_protocol_vhost_options pvo_opt_default = {
        NULL, NULL, "default", "1"
    };
    static const struct lws_protocol_vhost_options pvo_ws = {
        NULL, &pvo_opt_default, "ws", ""
    };

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port                  = port;
    info.protocols             = g.protocols;
    info.iface                 = NULL;
    info.foreign_loops         = (void **)&uv_loop;
    info.options               = LWS_SERVER_OPTION_LIBUV;
    info.pvo                   = &pvo_ws;
    info.retry_and_idle_policy = &s_retry_policy;

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

static void driver_server_stop(void)
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
 * driver_http_set / driver_send / driver_ws_send_all
 * Internal transport functions — called by WebLoop (service_loopback.c) only.
 * driver_send despacha pelo tipo da conexão: ws, stream ou resposta http.
 * ---------------------------------------------------------------------- */
static ws_session_t *ws_session_by_conn_id(uint32_t conn_id)
{
    for (int i = 0; i < g.ws_count; i++)
        if (g.ws_ids[i] == conn_id) return g.ws_sessions[i];
    return NULL;
}

static void driver_http_set(uint32_t conn_id, gdweb_http_cmd_t cmd, const gdweb_value_t *value)
{
    if (!g.started) return;
    struct lws *wsi = wsi_by_conn_id(conn_id);
    if (!wsi || ws_session_by_conn_id(conn_id)) return;
    http_session_t *s = (http_session_t *)lws_wsi_user(wsi);
    if (!s) return;

    switch (cmd) {
        case GDWEB_HTTP_STATUS:
            if (value) s->status = (int)value->i64;
            break;

        case GDWEB_HTTP_CONTENT_TYPE:
            if (value && value->str) {
                strncpy(s->content_type, value->str, sizeof(s->content_type) - 1);
                s->content_type[sizeof(s->content_type) - 1] = '\0';
            }
            break;

        case GDWEB_HTTP_STREAM:
            session_stream_upgrade(wsi, s, NULL);
            break;

        default:
            break;
    }
}

static void driver_send(uint32_t conn_id, const char *data, size_t len)
{
    if (!g.started || !conn_id) return;

    ws_session_t *ws = ws_session_by_conn_id(conn_id);
    if (ws) {
        struct lws *wsi = wsi_by_conn_id(conn_id);
        if (wsi && data && len) _ws_send_wsi(wsi, data, len);
        return;
    }

    struct lws *wsi = wsi_by_conn_id(conn_id);
    if (!wsi) return;
    http_session_t *s = (http_session_t *)lws_wsi_user(wsi);
    if (!s) return;

    if (s->is_stream) {
        if (s->ring_buf && data && len) {
            ring_write(s, (const uint8_t *)data, (int)len);
            lws_callback_on_writable(wsi);
        }
        return;
    }

    free(s->body);
    s->body     = NULL;
    s->body_len = 0;
    if (data && len) {
        s->body = malloc(len);
        if (s->body) {
            memcpy(s->body, data, len);
            s->body_len = len;
        }
    }
    s->has_response = 1;
    lws_callback_on_writable(wsi);
}

static void driver_ws_send_all(const char *path, const char *data, size_t len,
                        uint32_t exclude_conn_id)
{
    if (!g.started || !data || !len) return;
    for (int i = 0; i < g.ws_count; i++) {
        if (g.ws_ids[i] == exclude_conn_id) continue;
        if (path) {
            ws_session_t *s = g.ws_sessions[i];
            if (!s || strcmp(s->path, path) != 0) continue;
        }
        struct lws *wsi = wsi_by_conn_id(g.ws_ids[i]);
        if (wsi) _ws_send_wsi(wsi, data, len);
    }
}

static void driver_close(uint32_t conn_id)
{
    if (!g.started || !conn_id) return;
    struct lws *wsi = wsi_by_conn_id(conn_id);
    if (!wsi) return;

    ws_session_t *ws = ws_session_by_conn_id(conn_id);
    if (ws) {
        /* fecha limpo: drena pendencias e manda close frame no WRITEABLE */
        ws->close_pending = 1;
        lws_callback_on_writable(wsi);
        return;
    }
    /* conexao http/stream: derruba assincrono */
    lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
}

static void driver_ws_close_all(const char *path, uint32_t exclude_conn_id)
{
    if (!g.started) {
        fprintf(stderr, "[webserver] close_all ignorado: server parado\n");
        return;
    }
    int matched = 0;
    for (int i = 0; i < g.ws_count; i++) {
        if (g.ws_ids[i] == exclude_conn_id) continue;
        if (path) {
            ws_session_t *s = g.ws_sessions[i];
            if (!s || strcmp(s->path, path) != 0) continue;
        }
        driver_close(g.ws_ids[i]);
        matched++;
    }
    fprintf(stderr, "[webserver] close_all '%s': %d de %d conexoes ws\n",
            path ? path : "*", matched, g.ws_count);
}

__attribute__((constructor))
static void init(void)
{
    gecnd_registry("set", "web_driver:server_start",    (void *)driver_server_start,  NULL);
    gecnd_registry("set", "web_driver:server_stop",     (void *)driver_server_stop,   NULL);
    gecnd_registry("set", "web_driver:server_http",     (void *)driver_http_set,      NULL);
    gecnd_registry("set", "web_driver:server_send",      (void *)driver_send,         NULL);
    gecnd_registry("set", "web_driver:server_send_all",  (void *)driver_ws_send_all,  NULL);
    gecnd_registry("set", "web_driver:server_close",     (void *)driver_close,        NULL);
    gecnd_registry("set", "web_driver:server_close_all", (void *)driver_ws_close_all, NULL);
}
