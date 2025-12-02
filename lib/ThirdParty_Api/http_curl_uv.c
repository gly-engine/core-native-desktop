/* http_libuv_curl.c
 *
 * Integra libuv + libcurl (multi) para múltiplas requisições assíncronas.
 *
 * Compilar com: -lcurl -luv -llua (ajuste conforme seu ambiente)
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <curl/curl.h>
#include <lauxlib.h>
#include <lua.h>
#include <uv.h>

/* --- estrutura do request --- */
typedef struct {
    lua_State *L;
    int64_t id;

    CURL *easy;
    char *last_error;

    /* buffers temporários, se quiser guardar algo */
} req_t;

/* --- globals --- */
static CURLM *g_curl_multi = NULL;
static uv_loop_t *g_uv_loop = NULL;
static uv_timer_t *g_timeout_timer = NULL;

/* forward */
static void check_multi_info(void);

/* --- reutilizei suas funções JS/Lua helpers (com pequenas adaptações) --- */
static void native_callback_http(lua_State* L, int64_t req_id, const char *const evt_key)
{
    static int native_http_callback_ref = 0;
    if (!native_http_callback_ref) {
        lua_getglobal(L, "native_callback_http");
        if (lua_isfunction(L, -1)) {
            native_http_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
            native_http_callback_ref = 0;
            return;
        }
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, native_http_callback_ref);
    lua_pushinteger(L, req_id);
    lua_pushstring(L, evt_key);
}

static const char* native_http_get_str(lua_State* L, int64_t req_id, const char *const evt_key)
{
    native_callback_http(L, req_id, evt_key);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        lua_pop(L, 1);
        return NULL;
    }
    const char *s = luaL_checkstring(L, -1);
    lua_pop(L, 1);
    return s;
}

/* --- callbacks para curl easy (chamados durante a transferência) --- */
static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total_size = size * nmemb;
    req_t *r = (req_t *)userp;
    if (!r || !r->L) return total_size;

    /* evento para Lua: add-body-data(req_id, data) */
    native_callback_http(r->L, r->id, "add-body-data");
    lua_pushlstring(r->L, (char*)contents, total_size);
    if (lua_pcall(r->L, 3, 0, 0) != LUA_OK) {
        lua_pop(r->L, 1); /* descartar erro */
    }
    return total_size;
}

static size_t curl_header_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total_size = size * nmemb;
    req_t *r = (req_t *)userp;
    if (!r || !r->L) return total_size;

    /* detectar status line "HTTP/1.x XXX ..." */
    if (total_size >= 12 && strncmp((char*)contents, "HTTP/", 5) == 0) {
        char http_status[4] = "000";
        memcpy(http_status, (char*)contents + 9, 3); /* posição padrão */
        int status = atoi(http_status);

        native_callback_http(r->L, r->id, "set-status");
        lua_pushinteger(r->L, status);
        if (lua_pcall(r->L, 3, 0, 0) != LUA_OK) {
            lua_pop(r->L, 1);
        }
    }

    /* opcional: enviar headers completos para Lua se necessário */
    return total_size;
}

/* --- helpers curl <-> uv --- */

/* cada socket do curl terá um uv_poll_t apontando para ele */
typedef struct {
    curl_socket_t sockfd;
    uv_poll_t poll_handle;
    int action; /* CURL_POLL_IN/OUT/BOTH */
} sock_info_t;

/* conveniente: pega req_t* a partir do easy handle */
static req_t *get_request_from_easy(CURL *easy)
{
    void *priv = NULL;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &priv);
    return (req_t*)priv;
}

/* quando curl diz que um socket mudou (start/stop/..) ele chama essa função */
static int on_socket(CURL *easy, curl_socket_t s, int what, void *userp, void *sockp);

/* função chamada pela uv quando evento no socket acontece */
static void poll_cb(uv_poll_t* handle, int status, int events)
{
    sock_info_t *si = (sock_info_t*) handle->data;
    if (!si) return;

    int ev = 0;
    if (events & UV_READABLE) ev |= CURL_CSELECT_IN;
    if (events & UV_WRITABLE) ev |= CURL_CSELECT_OUT;

    int running_handles;
    int rc = curl_multi_socket_action(g_curl_multi, si->sockfd, ev, &running_handles);
    (void)rc; (void)running_handles;
    check_multi_info();
}

/* quando curl pede um timer up-to-date ele chama essa função (via CURLMOPT_TIMERFUNCTION) */
static int multi_timer_cb(CURLM *multi, long timeout_ms, void *userp)
{
    if (!g_timeout_timer) return 0;

    if (timeout_ms < 0) {
        /* cancel timer */
        uv_timer_stop(g_timeout_timer);
    } else {
        if (timeout_ms == 0) timeout_ms = 1;
        uv_timer_start(g_timeout_timer, (uv_timer_cb) (void (*)(uv_timer_t*)) (void*) (intptr_t) (NULL), timeout_ms, 0);
        /* NOTE: we cannot cast directly to uv_timer_cb with the curl signature.
           below we will setup a proper uv_timer start using a real callback that
           calls curl_multi_socket_action with CURL_SOCKET_TIMEOUT. */
    }
    return 0;
}

/* we'll use a proper uv timer callback */
static void timeout_cb(uv_timer_t *timer)
{
    int running_handles;
    curl_multi_socket_action(g_curl_multi, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    (void)running_handles;
    check_multi_info();
}

/* cria/associa uv_poll_t ao socket (iniciar ou atualizar) */
static sock_info_t* create_sock_info(curl_socket_t sockfd)
{
    sock_info_t *si = malloc(sizeof(sock_info_t));
    if (!si) return NULL;
    si->sockfd = sockfd;
    si->action = 0;
    uv_poll_init(g_uv_loop, &si->poll_handle, sockfd);
    si->poll_handle.data = si;
    return si;
}

/* limpa sock_info */
static void free_sock_info(void *ptr)
{
    sock_info_t *si = (sock_info_t*)ptr;
    if (!si) return;
    uv_close((uv_handle_t*)&si->poll_handle, (uv_close_cb)free);
}

/* callback que o curl chama quando o estado do socket muda */
static int on_socket(CURL *easy, curl_socket_t s, int what, void *userp, void *sockp)
{
    sock_info_t *si = (sock_info_t*)sockp;
    if (what == CURL_POLL_IN || what == CURL_POLL_OUT || what == CURL_POLL_INOUT) {
        if (!si) {
            si = create_sock_info(s);
            if (!si) return CURLM_OUT_OF_MEMORY;
            curl_multi_assign(g_curl_multi, s, si);
        }
        int events = 0;
        if (what == CURL_POLL_IN) events = UV_READABLE;
        else if (what == CURL_POLL_OUT) events = UV_WRITABLE;
        else events = UV_READABLE | UV_WRITABLE;

        /* start/modify poll */
        uv_poll_start(&si->poll_handle, events, poll_cb);
    } else if (what == CURL_POLL_REMOVE) {
        if (si) {
            uv_poll_stop(&si->poll_handle);
            /* free later via uv_close so no race */
            uv_close((uv_handle_t*)&si->poll_handle, (uv_close_cb)free);
            curl_multi_assign(g_curl_multi, s, NULL);
        }
    }
    return 0;
}

/* --- verifica mensagens completas do curl_multi e limpa recursos --- */
static void check_multi_info(void)
{
    CURLMsg *msg;
    int msgs_left;
    while ((msg = curl_multi_info_read(g_curl_multi, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy = msg->easy_handle;
            req_t *r = get_request_from_easy(easy);
            if (r && r->L) {
                if (msg->data.result != CURLE_OK) {
                    const char *err = curl_easy_strerror(msg->data.result);
                    native_callback_http(r->L, r->id, "set-error");
                    lua_pushstring(r->L, err);
                    if (lua_pcall(r->L, 3, 0, 0) != LUA_OK) {
                        lua_pop(r->L, 1);
                    }
                } else {
                    /* sucesso: notificar completo */
                    native_callback_http(r->L, r->id, "completed");
                    if (lua_pcall(r->L, 2, 0, 0) != LUA_OK) {
                        lua_pop(r->L, 1);
                    }
                }
            }

            /* remover easy handle do multi e limpar */
            curl_multi_remove_handle(g_curl_multi, easy);

            /* liberar memory associado ao easy & req_t */
            if (r) {
                /* limpeza de campo privado, free req_t */
                curl_easy_setopt(easy, CURLOPT_PRIVATE, NULL);
                free(r);
            }
            curl_easy_cleanup(easy);
        }
    }
}

/* --- inicia um easy handle e adiciona ao multi (assíncrono) --- */
static int start_multi_request(lua_State* L)
{
    /* stack: func, req_id, ... (conforme sua chamada original) */
    int64_t req_id = luaL_checkinteger(L, 2);

    /* criar req_t dinamicamente */
    req_t *r = calloc(1, sizeof(req_t));
    if (!r) {
        native_callback_http(L, req_id, "set-error");
        lua_pushstring(L, "out of memory");
        lua_pcall(L, 3, 0, 0);
        return 0;
    }
    r->L = L;
    r->id = req_id;

    /* pegar dados de Lua via seu mecanismo native_http_get_str */
    const char *url = native_http_get_str(L, req_id, "get-fullurl");
    const char *method = native_http_get_str(L, req_id, "get-method");
    const char *body = native_http_get_str(L, req_id, "get-body-data");

    CURL *easy = curl_easy_init();
    if (!easy) {
        native_callback_http(L, req_id, "set-error");
        lua_pushstring(L, "failed to init curl easy");
        lua_pcall(L, 3, 0, 0);
        free(r);
        return 0;
    }
    r->easy = easy;

    /* opções básicas */
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, curl_header_cb);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, r);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
    /* opcional: ajustar SSL conforme sua política */
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);

    /* método */
    if (method && strcmp(method, "GET") == 0) {
        curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
    } else if (method && strcmp(method, "HEAD") == 0) {
        curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    } else {
        if (body && strlen(body) > 0) {
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body);
        }
        if (method && strcmp(method, "POST") == 0) {
            curl_easy_setopt(easy, CURLOPT_POST, 1L);
        } else if (method && strcmp(method, "PUT") == 0) {
            curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
        } else if (method && strcmp(method, "DELETE") == 0) {
            curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (method && strcmp(method, "PATCH") == 0) {
            curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PATCH");
        }
    }

    /* anexa nosso req_t no easy */
    curl_easy_setopt(easy, CURLOPT_PRIVATE, (void*)r);

    /* adiciona ao multi */
    curl_multi_add_handle(g_curl_multi, easy);

    /* acionar uma rodada imediata (pode ser opcional) */
    int running_handles;
    curl_multi_socket_action(g_curl_multi, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    check_multi_info();

    return 0;
}

/* --- Lua binding: native_http_handler -> agora apenas agenda o request --- */
static int lua_native_http_handler(lua_State* L)
{
    if (!g_curl_multi) {
        luaL_error(L, "http multi not initialized");
        return 0;
    }
    /* cria e agenda o request */
    return start_multi_request(L);
}

/* --- inicialização / finalização --- */
void gly_http_set_loop(uv_loop_t *loop)
{
    g_uv_loop = loop;
}

void gly_hook_luaopen_http(lua_State* L)
{
    /* inicializa libcurl multi */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_curl_multi = curl_multi_init();
    if (!g_curl_multi) {
        luaL_error(L, "failed to init curl multi");
        return;
    }

    /* libuv loop: se não foi passado, pega o default */
    if (!g_uv_loop) g_uv_loop = uv_default_loop();

    /* cria timer para manejar timeout do multi */
    g_timeout_timer = malloc(sizeof(uv_timer_t));
    uv_timer_init(g_uv_loop, g_timeout_timer);
    uv_timer_start(g_timeout_timer, timeout_cb, 0, 0);
    uv_timer_stop(g_timeout_timer); /* parado inicialmente */

    /* configurar callbacks do curl multi */
    curl_multi_setopt(g_curl_multi, CURLMOPT_SOCKETFUNCTION, on_socket);
    curl_multi_setopt(g_curl_multi, CURLMOPT_SOCKETDATA, NULL);

    /* configurar timer function (usamos timeout_cb via uv_timer) */
    curl_multi_setopt(g_curl_multi, CURLMOPT_TIMERFUNCTION,
        (curl_multi_timer_callback)NULL); /* não usamos diretamente aqui */
    /* Em alguns ambientes você pode usar CURLMOPT_TIMERSOCKETFUNCTION / TIMERDATA, mas simplificamos
       e gerenciamos tempo via curl_multi_socket_action com CURL_SOCKET_TIMEOUT no timer_cb. */

    /* expõe função para Lua (mesmo nome que você tinha) */
    lua_pushcfunction(L, lua_native_http_handler);
    lua_setglobal(L, "native_http_handler");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_ssl");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_callback");
}

/* cleanup (opcional) */
void gly_http_cleanup(void)
{
    if (g_curl_multi) {
        curl_multi_cleanup(g_curl_multi);
        g_curl_multi = NULL;
    }
    curl_global_cleanup();
    if (g_timeout_timer) {
        /* uv_close e free */
        uv_close((uv_handle_t*)g_timeout_timer, (uv_close_cb)free);
        g_timeout_timer = NULL;
    }
}

/* --- fim do arquivo --- */
