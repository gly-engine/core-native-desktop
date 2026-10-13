#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <uv.h>

#include "gecnd.h"
#include "gdweb.h"

/* [registry.web_http_cgi] opkg = "/opt/zedia/bin/opkg"
 * registra a rota "/opkg"; cada request roda o programa como CGI/1.1
 * (variáveis de ambiente + stdout com cabeçalhos "Status:"/"Content-Type:").
 *
 * A mesma rota aceita websocket, no modelo do websocketd: a query vem na URL
 * do upgrade, o programa roda com HTTP_UPGRADE=websocket e cada linha do
 * stdout vira uma mensagem; o socket fecha quando o programa termina. Se o
 * cliente sair antes, o programa segue até o fim (um opkg install não pode
 * parar pela metade), só que sem ninguém ouvindo. */

#define CGI_OUT_MAX (16u * 1024u * 1024u)

typedef struct cgi_job {
    uv_process_t    proc;
    uv_pipe_t       out;
    gdweb_id_t      id;       /* ws: 0 depois que o cliente saiu */
    char           *buf;
    size_t          len;
    size_t          cap;
    int             closing;
    struct cgi_job *next;     /* ws: lista para o CLOSE achar o job pelo id */
    unsigned        exited    : 1;
    unsigned        eof       : 1;
    unsigned        truncated : 1;
    unsigned        ws        : 1;
} cgi_job_t;

static uv_loop_t *s_loop;
static cgi_job_t *s_ws_jobs;

static void cgi_reply(gdweb_id_t id, int status, const char *type, const char *body, size_t len)
{
    gdweb_value_t st = { .i64 = status };
    gdweb_value_t ct = { .str = type };
    gdweb_control_server()->http(id, GDWEB_HTTP_STATUS, &st);
    gdweb_control_server()->http(id, GDWEB_HTTP_CONTENT_TYPE, &ct);
    gdweb_control_server()->send(id, body, len);
}

static void cgi_fail(gdweb_id_t id, const char *msg)
{
    cgi_reply(id, 502, "text/plain; charset=utf-8", msg, strlen(msg));
}

/* separa os cabeçalhos CGI (até a linha vazia) do corpo */
static void cgi_respond(cgi_job_t *job)
{
    if (job->truncated) {
        cgi_fail(job->id, "cgi: output too large");
        return;
    }

    char  *out  = job->buf ? job->buf : "";
    size_t len  = job->len;
    char  *body = NULL;
    for (size_t i = 0; i + 1 < len; i++) {
        if (out[i] == '\n' && out[i + 1] == '\n')                         { body = out + i + 2; break; }
        if (out[i] == '\n' && out[i + 1] == '\r' && i + 2 < len && out[i + 2] == '\n') { body = out + i + 3; break; }
    }
    if (!body) {
        cgi_fail(job->id, "cgi: missing headers");
        return;
    }

    int  status = 200;
    char type[128] = "text/plain; charset=utf-8";
    for (char *line = out; line < body;) {
        char *end = memchr(line, '\n', (size_t)(body - line));
        if (!end) break;
        char *stop = (end > line && end[-1] == '\r') ? end - 1 : end;
        *stop = '\0';
        char *value = strchr(line, ':');
        if (value) {
            *value++ = '\0';
            while (*value == ' ') value++;
            if (strcasecmp(line, "Status") == 0)       status = atoi(value);
            else if (strcasecmp(line, "Content-Type") == 0) snprintf(type, sizeof(type), "%s", value);
        }
        line = end + 1;
    }

    cgi_reply(job->id, status ? status : 200, type, body, len - (size_t)(body - out));
}

/* ws: cada linha completa vira uma mensagem; com final, o resto sem '\n' também */
static void cgi_ws_lines(cgi_job_t *job, int final)
{
    if (!job->len) return;
    size_t start = 0;
    for (size_t i = 0; i <= job->len; i++) {
        if (i < job->len ? job->buf[i] != '\n' : !final) continue;
        size_t n = i - start;
        if (n && job->buf[start + n - 1] == '\r') n--;
        if (n && job->id) gdweb_control_server()->send(job->id, job->buf + start, n);
        start = i < job->len ? i + 1 : i;
    }
    memmove(job->buf, job->buf + start, job->len - start);
    job->len -= start;
}

static void cgi_ws_unlink(cgi_job_t *job)
{
    for (cgi_job_t **it = &s_ws_jobs; *it; it = &(*it)->next) {
        if (*it == job) { *it = job->next; return; }
    }
}

static void cgi_closed(uv_handle_t *h)
{
    cgi_job_t *job = h->data;
    if (--job->closing > 0) return;
    if (job->ws) cgi_ws_unlink(job);
    free(job->buf);
    free(job);
}

static void cgi_finish(cgi_job_t *job)
{
    if (!job->exited || !job->eof) return;
    if (!job->ws) {
        cgi_respond(job);
    } else {
        cgi_ws_lines(job, 1);
        if (job->id) gdweb_control_server()->close(job->id);
    }
    job->closing = 2;
    uv_close((uv_handle_t *)&job->proc, cgi_closed);
    uv_close((uv_handle_t *)&job->out,  cgi_closed);
}

static void cgi_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *buf)
{
    cgi_job_t *job = h->data;
    if (job->cap - job->len < suggested) {
        size_t cap = job->cap ? job->cap * 2 : 64 * 1024;
        while (cap - job->len < suggested) cap *= 2;
        char *grown = realloc(job->buf, cap);
        if (!grown) { buf->base = NULL; buf->len = 0; return; }
        job->buf = grown;
        job->cap = cap;
    }
    buf->base = job->buf + job->len;
    buf->len  = job->cap - job->len;
}

static void cgi_read(uv_stream_t *s, ssize_t n, const uv_buf_t *buf)
{
    (void)buf;
    cgi_job_t *job = s->data;
    if (n > 0) {
        job->len += (size_t)n;
        if (job->ws) cgi_ws_lines(job, 0);
        if (job->len > CGI_OUT_MAX) {
            job->truncated = 1;
            uv_process_kill(&job->proc, SIGTERM);
        }
        return;
    }
    if (n < 0) {
        uv_read_stop(s);
        job->eof = 1;
        cgi_finish(job);
    }
}

static void cgi_exit(uv_process_t *p, int64_t status, int signal)
{
    (void)status; (void)signal;
    cgi_job_t *job = p->data;
    job->exited = 1;
    cgi_finish(job);
}

static int env_add(char ***env, int *count, int *cap, const char *name, const char *value)
{
    if (*count + 2 > *cap) {
        int    ncap = *cap ? *cap * 2 : 64;
        char **grown = realloc(*env, (size_t)ncap * sizeof(char *));
        if (!grown) return -1;
        *env = grown;
        *cap = ncap;
    }
    size_t n = strlen(name) + strlen(value) + 2;
    char  *item = malloc(n);
    if (!item) return -1;
    snprintf(item, n, "%s=%s", name, value);
    (*env)[(*count)++] = item;
    (*env)[*count]     = NULL;
    return 0;
}

/* ws: a falha vai como uma linha de texto e o socket fecha, como um programa
 * que só imprimiu o erro */
static void cgi_abort(gdweb_id_t id, int ws, const char *msg)
{
    if (!ws) {
        cgi_fail(id, msg);
        return;
    }
    gdweb_control_server()->send(id, msg, strlen(msg));
    gdweb_control_server()->close(id);
}

/* path: "/<nome>[/path_info][?query]"; query != NULL substitui a do path
 * (no ws o driver entrega a query separada) */
static void cgi_spawn(gdweb_id_t id, const char *path, const char *query, const char *method,
                      size_t body_len, const char *content_type, int ws)
{
    path = path ? path : "/";
    const char *name  = (*path == '/') ? path + 1 : path;
    size_t      nlen  = strcspn(name, "/?");
    const char *rest  = name + nlen;
    if (!query) {
        query = strchr(path, '?');
        query = query ? query + 1 : "";
    }

    char key[128];
    snprintf(key, sizeof(key), "web_http_cgi:%.*s", (int)nlen, name);
    const char *program = NULL;
    gecnd_registry("get", key, (void *)&program, NULL);
    if (!program || !s_loop) {
        cgi_abort(id, ws, "cgi: program not configured");
        return;
    }

    char script[160], path_info[512];
    snprintf(script, sizeof(script), "/%.*s", (int)nlen, name);
    snprintf(path_info, sizeof(path_info), "%.*s", (int)strcspn(rest, "?"), rest);

    char         **env = NULL;
    int            env_count = 0, env_cap = 0;
    uv_env_item_t *items = NULL;
    int            item_count = 0;
    if (uv_os_environ(&items, &item_count) == 0) {
        for (int i = 0; i < item_count; i++)
            env_add(&env, &env_count, &env_cap, items[i].name, items[i].value);
        uv_os_free_environ(items, item_count);
    }
    char clen[32];
    snprintf(clen, sizeof(clen), "%zu", body_len);
    env_add(&env, &env_count, &env_cap, "GATEWAY_INTERFACE", "CGI/1.1");
    env_add(&env, &env_count, &env_cap, "SERVER_PROTOCOL", "HTTP/1.1");
    env_add(&env, &env_count, &env_cap, "SERVER_SOFTWARE", "gecnd");
    env_add(&env, &env_count, &env_cap, "REQUEST_METHOD", method ? method : "GET");
    env_add(&env, &env_count, &env_cap, "SCRIPT_NAME", script);
    env_add(&env, &env_count, &env_cap, "PATH_INFO", path_info);
    env_add(&env, &env_count, &env_cap, "QUERY_STRING", query);
    env_add(&env, &env_count, &env_cap, "CONTENT_LENGTH", clen);
    env_add(&env, &env_count, &env_cap, "CONTENT_TYPE", content_type ? content_type : "");
    if (ws) env_add(&env, &env_count, &env_cap, "HTTP_UPGRADE", "websocket");

    cgi_job_t *job = calloc(1, sizeof(*job));
    char      *args[] = { (char *)program, NULL };
    int        err = job ? 0 : UV_ENOMEM;
    if (!err) {
        job->id = id;
        job->ws = ws ? 1 : 0;
        job->proc.data = job;
        job->out.data  = job;
        uv_pipe_init(s_loop, &job->out, 0);

        uv_stdio_container_t stdio[3];
        stdio[0].flags = UV_IGNORE;
        stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        stdio[1].data.stream = (uv_stream_t *)&job->out;
        stdio[2].flags = UV_INHERIT_FD;
        stdio[2].data.fd = 2;

        uv_process_options_t opts = {0};
        opts.file        = program;
        opts.args        = args;
        opts.env         = env;
        opts.exit_cb     = cgi_exit;
        opts.stdio       = stdio;
        opts.stdio_count = 3;
        err = uv_spawn(s_loop, &job->proc, &opts);
    }

    for (int i = 0; i < env_count; i++) free(env[i]);
    free(env);

    if (err) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cgi: cannot run %s: %s", program, uv_strerror(err));
        cgi_abort(id, ws, msg);
        if (job) {
            job->ws = 0;
            job->closing = 1;
            uv_close((uv_handle_t *)&job->out, cgi_closed);
        }
        return;
    }
    if (job->ws) {
        job->next = s_ws_jobs;
        s_ws_jobs = job;
    }
    uv_read_start((uv_stream_t *)&job->out, cgi_alloc, cgi_read);
}

static void http_cgi(const gdweb_http_req_t *req)
{
    cgi_spawn(req->id, req->path, NULL, req->method, req->body_len, req->content_type, 0);
}

static void ws_cgi(const gdweb_ws_req_t *req)
{
    if (req->event == GDWEB_WS_OPEN) {
        char query[2048];
        snprintf(query, sizeof(query), "%.*s", (int)req->len, req->data ? req->data : "");
        cgi_spawn(req->id, req->path, query, "GET", 0, NULL, 1);
        return;
    }
    /* o programa segue até o fim; só para de mandar para um id que já saiu */
    if (req->event == GDWEB_WS_CLOSE) {
        for (cgi_job_t *job = s_ws_jobs; job; job = job->next)
            if (job->id == req->id) job->id = 0;
    }
}

static void register_cgi_route(const char *key, void *value, void *usr)
{
    (void)value; (void)usr;
    const char *name = key + sizeof("web_http_cgi:") - 1;
    char route[160];
    snprintf(route, sizeof(route), "web_http_route:%s", name);
    gecnd_registry("set", route, (void *)http_cgi, "strdup=key");
    snprintf(route, sizeof(route), "web_ws_route:%s", name);
    gecnd_registry("set", route, (void *)ws_cgi, "strdup=key");
}

static void on_web_loop(const char *key, void *value, void *usr)
{
    (void)key; (void)usr;
    s_loop = value;
    if (s_loop) gecnd_registry("get", "web_http_cgi:*", (void *)register_cgi_route, NULL);
}

__attribute__((constructor))
static void register_cgi(void)
{
    gecnd_registry("hook", "web_loop", (void *)on_web_loop, NULL);
}
