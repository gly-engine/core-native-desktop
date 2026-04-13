#include "gamely_webserver.h"

#include <libwebsockets.h>
#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "khash.h"
KHASH_MAP_INIT_INT(conn_map, struct lws *)

#define MAX_ROUTES     64
#define MAX_WS_CLIENTS 64
#define MSG_BUF        4096

/* -----------------------------------------------------------------------
 * Rotas — mantidas ordenadas por (type, path) para busca binária
 * ---------------------------------------------------------------------- */
typedef enum { ROUTE_HTTP, ROUTE_WS } route_type_t;

typedef struct {
    char          path[128];
    route_type_t  type;
    gly_http_cb_t http_cb;
    gly_ws_cb_t   ws_cb;
    char          proxy_to[128];
} route_t;

/* -----------------------------------------------------------------------
 * Sessões (alocadas pelo lws via per_session_data_size — sem heap próprio)
 * ---------------------------------------------------------------------- */
typedef struct {
    gly_conn_id_t  conn_id;
    int            has_response;
    int            status;
    char           content_type[64];
    unsigned char  body[MSG_BUF];
    size_t         body_len;
} http_session_t;

typedef struct {
    unsigned char  pending[LWS_PRE + MSG_BUF];
    size_t         pending_len;
    int            has_pending;
    route_t       *route;
    gly_conn_id_t  conn_id;
} ws_session_t;

/* -----------------------------------------------------------------------
 * Singleton — tudo estático, sem heap próprio exceto khash
 * ---------------------------------------------------------------------- */
static struct {
    struct lws_context  *ctx;
    struct lws_protocols protocols[3];

    route_t              routes[MAX_ROUTES];
    int                  route_count;

    khash_t(conn_map)   *conn_map;   /* único heap: alocado em start, liberado em stop */

    gly_conn_id_t        ws_ids[MAX_WS_CLIENTS];
    ws_session_t        *ws_sessions[MAX_WS_CLIENTS]; /* ponteiros para sessões do lws */
    int                  ws_count;

    gly_conn_id_t        next_id;
    int                  started;
} g;

/* -----------------------------------------------------------------------
 * Busca binária de rota — O(log n)
 * Rotas são inseridas já ordenadas por (type ASC, path ASC).
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

/* Insere mantendo a ordem (insertion sort — MAX_ROUTES é pequeno) */
static route_t *insert_route(const char *path, route_type_t type)
{
    if (g.route_count >= MAX_ROUTES) {
        fprintf(stderr, "[webserver] MAX_ROUTES atingido\n");
        return NULL;
    }
    /* acha posição de inserção */
    int pos = g.route_count;
    for (int i = 0; i < g.route_count; i++) {
        int c = (int)type - (int)g.routes[i].type;
        if (c == 0) c = strcmp(path, g.routes[i].path);
        if (c < 0) { pos = i; break; }
        if (c == 0) return &g.routes[i]; /* rota já existe — atualiza */
    }
    /* desloca para abrir espaço */
    memmove(&g.routes[pos + 1], &g.routes[pos],
            sizeof(route_t) * (size_t)(g.route_count - pos));
    g.route_count++;
    memset(&g.routes[pos], 0, sizeof(route_t));
    strncpy(g.routes[pos].path, path, sizeof(g.routes[pos].path) - 1);
    g.routes[pos].type = type;
    return &g.routes[pos];
}

/* -----------------------------------------------------------------------
 * Resolve alias chain com detecção de ciclo via bitmap de visited
 * ---------------------------------------------------------------------- */
static route_t *resolve_route(route_t *r)
{
    /* bitmap de índices visitados — MAX_ROUTES <= 64 */
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
static gly_conn_id_t alloc_id(void)
{
    if (++g.next_id == 0) g.next_id = 1;
    return g.next_id;
}

static struct lws *wsi_by_id(gly_conn_id_t id)
{
    if (!id) return NULL;
    khint_t k = kh_get(conn_map, g.conn_map, id);
    if (k == kh_end(g.conn_map)) return NULL;
    return kh_val(g.conn_map, k);
}

static void conn_register(gly_conn_id_t id, struct lws *wsi)
{
    int ret;
    khint_t k = kh_put(conn_map, g.conn_map, id, &ret);
    assert(ret >= 0); /* ret<0 = erro de alocação do khash */
    kh_val(g.conn_map, k) = wsi;
}

static void conn_remove(gly_conn_id_t id)
{
    khint_t k = kh_get(conn_map, g.conn_map, id);
    if (k != kh_end(g.conn_map)) kh_del(conn_map, g.conn_map, k);
}

/* -----------------------------------------------------------------------
 * _ws_send_wsi — interno, agenda escrita via uv_poll_t
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

        const char *path = in ? (const char *)in : "/";
        printf("[HTTP] GET %s  id=%u\n", path, s->conn_id);

        route_t *r = resolve_route(find_route(path, ROUTE_HTTP));
        if (r && r->http_cb) {
            gly_http_req_t req = { .conn_id = s->conn_id, .path = path };
            r->http_cb(&req);
        } else {
            s->status   = 404;
            strncpy(s->content_type, "text/plain", sizeof(s->content_type) - 1);
            memcpy(s->body, "Not Found", 9);
            s->body_len     = 9;
            s->has_response = 1;
        }
        lws_callback_on_writable(wsi);
        return 0;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
        if (!s || !s->has_response) break;

        unsigned char hdr[512], *p = hdr, *end = hdr + sizeof(hdr);

        if (lws_add_http_header_status(wsi, (unsigned int)s->status, &p, end) ||
            lws_add_http_header_by_token(wsi,
                WSI_TOKEN_HTTP_CONTENT_TYPE,
                (unsigned char *)s->content_type,
                (int)strlen(s->content_type), &p, end) ||
            lws_add_http_header_content_length(wsi, s->body_len, &p, end) ||
            lws_finalize_http_header(wsi, &p, end))
            return -1;

        if (lws_write(wsi, hdr, (size_t)(p - hdr), LWS_WRITE_HTTP_HEADERS) < 0 ||
            lws_write(wsi, s->body, s->body_len, LWS_WRITE_HTTP_FINAL) < 0)
            return -1;

        s->has_response = 0;
        conn_remove(s->conn_id);
        lws_http_transaction_completed(wsi);
        return -1;
    }

    case LWS_CALLBACK_CLOSED_HTTP:
        /* garante remoção mesmo se fechar antes de HTTP_WRITEABLE */
        if (s && s->conn_id) conn_remove(s->conn_id);
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

        if (g.ws_count < MAX_WS_CLIENTS) {
            g.ws_ids[g.ws_count]      = s->conn_id;
            g.ws_sessions[g.ws_count] = s;
            g.ws_count++;
        } else {
            fprintf(stderr, "[webserver] MAX_WS_CLIENTS atingido, rejeitando\n");
            conn_remove(s->conn_id);
            return -1;
        }

        printf("[WS] CONECTADO %s id=%u total=%d\n",
               path, s->conn_id, g.ws_count);

        route_t *real = resolve_route(s->route);
        if (real && real->ws_cb) {
            gly_ws_req_t req = { .conn_id=s->conn_id, .event=GLY_WS_OPEN };
            real->ws_cb(&req);
        }
        break;
    }

    case LWS_CALLBACK_CLOSED: {
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

        printf("[WS] DESCONECTADO id=%u total=%d\n", s->conn_id, g.ws_count);

        route_t *real = resolve_route(s->route);
        if (real && real->ws_cb) {
            gly_ws_req_t req = { .conn_id=s->conn_id, .event=GLY_WS_CLOSE };
            real->ws_cb(&req);
        }
        /* zera a sessão — o lws pode reutilizar a memória */
        memset(s, 0, sizeof(*s));
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        printf("[WS] RECV id=%u: %.*s\n", s->conn_id, (int)len, (char *)in);
        route_t *real = resolve_route(s->route);
        if (real && real->ws_cb) {
            gly_ws_req_t req = {
                .conn_id = s->conn_id,
                .event   = GLY_WS_MESSAGE,
                .data    = (const char *)in,
                .len     = len
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
 * API pública
 * ---------------------------------------------------------------------- */
void gamely_daemon_webserver_route_http(const char *path, gly_http_cb_t cb)
{
    assert(!g.started && "registre rotas antes de start()");
    route_t *r = insert_route(path, ROUTE_HTTP);
    if (!r) return;
    r->http_cb = cb;
    printf("[webserver] HTTP  %s\n", path);
}

void gamely_daemon_webserver_route_ws(const char *path, gly_ws_cb_t cb)
{
    assert(!g.started && "registre rotas antes de start()");
    route_t *r = insert_route(path, ROUTE_WS);
    if (!r) return;
    r->ws_cb = cb;
    printf("[webserver] WS    %s\n", path);
}

void gamely_daemon_webserver_proxy_http(const char *from, const char *to)
{
    assert(!g.started && "registre rotas antes de start()");
    route_t *r = insert_route(from, ROUTE_HTTP);
    if (!r) return;
    strncpy(r->proxy_to, to, sizeof(r->proxy_to) - 1);
    printf("[webserver] HTTP  %s → %s\n", from, to);
}

void gamely_daemon_webserver_proxy_ws(const char *from, const char *to)
{
    assert(!g.started && "registre rotas antes de start()");
    route_t *r = insert_route(from, ROUTE_WS);
    if (!r) return;
    strncpy(r->proxy_to, to, sizeof(r->proxy_to) - 1);
    printf("[webserver] WS    %s → %s\n", from, to);
}

void gamely_daemon_webserver_start(void *loop, int port)
{
    assert(!g.started);
    assert(loop);

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

    /*
     * lws_context_destroy remove todos os uv_poll_t/uv_timer_t
     * registrados no loop e libera toda memória interna do lws,
     * incluindo as sessões (http_session_t / ws_session_t).
     * Após isso os ponteiros em ws_sessions[] são inválidos.
     */
    if (g.ctx) {
        lws_context_destroy(g.ctx);
        g.ctx = NULL;
    }

    /* libera o único heap próprio: o khash */
    if (g.conn_map) {
        kh_destroy(conn_map, g.conn_map);
        g.conn_map = NULL;
    }

    /* zera o singleton inteiro — nulifica ws_sessions[] e tudo mais */
    memset(&g, 0, sizeof(g));
}

void gamely_ws_send(const char    *path,
                    gly_conn_id_t  conn_id,
                    const char    *text,
                    size_t         len,
                    gly_conn_id_t  exclude_id)
{
    if (!g.started || !text || !len) return;

    if (conn_id != 0) {
        struct lws *wsi = wsi_by_id(conn_id);
        if (wsi) _ws_send_wsi(wsi, text, len);
        return;
    }

    /* broadcast */
    for (int i = 0; i < g.ws_count; i++) {
        if (g.ws_ids[i] == exclude_id) continue;
        if (path) {
            ws_session_t *s = g.ws_sessions[i];
            if (!s || !s->route) continue;
            const char *rpath = s->route->path;
            const char *tpath = s->route->proxy_to[0] ? s->route->proxy_to : rpath;
            if (strcmp(rpath, path) != 0 && strcmp(tpath, path) != 0) continue;
        }
        struct lws *wsi = wsi_by_id(g.ws_ids[i]);
        if (wsi) _ws_send_wsi(wsi, text, len);
    }
}

void gamely_http_respond(gly_conn_id_t conn_id,
                         int           status,
                         const char   *content_type,
                         const char   *body,
                         size_t        body_len)
{
    if (!g.started) return;
    struct lws *wsi = wsi_by_id(conn_id);
    if (!wsi) return;
    http_session_t *s = (http_session_t *)lws_wsi_user(wsi);
    if (!s) return;

    s->status = status;
    strncpy(s->content_type, content_type, sizeof(s->content_type) - 1);
    size_t n = body_len < (MSG_BUF - 1) ? body_len : (MSG_BUF - 1);
    memcpy(s->body, body, n);
    s->body_len     = n;
    s->has_response = 1;
}
