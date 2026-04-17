#include "gecnd.h"
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVTOOLS_URL     "http://127.0.0.1:9222/json"
#define DEVTOOLS_RETRY   2000

static uv_process_t         proc;
static uv_process_options_t proc_opts;
static uv_timer_t           retry_timer;
static gly_req_id_t         ws_id;
static char                 http_body[4096];
static size_t               http_body_len;

/* ---- devtools webclient ---- */

static void on_ws_open(gly_req_id_t id, void *user) {
    ws_id = id;
    uv_timer_stop(&retry_timer);
    printf("[chromium] devtools connected\n");
}

static void on_ws_msg(gly_req_id_t id, const char *data, size_t len, void *user) {
    printf("[chromium] devtools: %.*s\n", (int)len, data);
}

static void on_ws_close(gly_req_id_t id, void *user) { ws_id = 0; }
static void on_ws_error(gly_req_id_t id, const char *msg, void *user) {}

static void on_http_done(gly_req_id_t id, void *user) {
    const char *p = strstr(http_body, "webSocketDebuggerUrl");
    if (!p) return;
    p = strstr(p, "ws://");
    if (!p) return;
    const char *end = strchr(p, '"');
    if (!end) return;

    char ws_url[256];
    size_t len = (size_t)(end - p);
    if (len >= sizeof(ws_url)) return;
    memcpy(ws_url, p, len);
    ws_url[len] = '\0';

    gamely_daemon_webclient_ws_connect(ws_url, NULL,
        on_ws_open, on_ws_msg, on_ws_close, on_ws_error, NULL);
}

static void on_http_data(gly_req_id_t id, const char *data, size_t len, void *user) {
    if (http_body_len + len < sizeof(http_body)) {
        memcpy(http_body + http_body_len, data, len);
        http_body_len += len;
        http_body[http_body_len] = '\0';
    }
}

static void on_http_status(gly_req_id_t id, int status, void *user) { http_body_len = 0; }
static void on_http_error (gly_req_id_t id, const char *msg, void *user) {}

static void try_connect_devtools(uv_timer_t *t) {
    if (ws_id) return;
    http_body_len = 0;
    gly_http_req_t req = {0};
    gamely_daemon_webclient_http(DEVTOOLS_URL, &req,
        on_http_status, on_http_data, on_http_done, on_http_error, NULL);
}

/* ---- browser spawn ---- */

static void on_browser_exit(uv_process_t *p, int64_t status, int signal) {
    gecnd_t *gly = gecnd_get_root();
    if (gly) gly->internal &= ~GECND_INTERNAL_BROWSER;
    ws_id = 0;
    uv_timer_stop(&retry_timer);
    printf("[chromium] exited status=%lld\n", (long long)status);
}

static void open_browser(const char *url) {
    gecnd_t *gly = gecnd_get_root();
    if (!gly || (gly->internal & GECND_INTERNAL_BROWSER)) return;

    /* TODO: build args from gecnd opts (bin, screen size, etc.) */
    char *args[] = {
        "chromium",
        (char *)url,
        "--remote-debugging-port=9222",
        "--remote-allow-origins=*",
        NULL
    };

    uv_stdio_container_t stdio[3] = {
        { .flags = UV_IGNORE },
        { .flags = UV_IGNORE },
        { .flags = UV_IGNORE },
    };

    memset(&proc_opts, 0, sizeof(proc_opts));
    proc_opts.exit_cb     = on_browser_exit;
    proc_opts.file        = args[0];
    proc_opts.args        = args;
    proc_opts.stdio_count = 3;
    proc_opts.stdio       = stdio;

    uv_loop_t *loop = gly->loop;
    if (uv_spawn(loop, &proc, &proc_opts) != 0) return;

    gly->internal |= GECND_INTERNAL_BROWSER;
    printf("[chromium] pid=%d url=%s\n", proc.pid, url);

    uv_timer_init(loop, &retry_timer);
    uv_timer_start(&retry_timer, try_connect_devtools, DEVTOOLS_RETRY, DEVTOOLS_RETRY);
}

/* ---- plugin entry ---- */

void init(void) {
    printf("[chromium] start plugin\n");
    open_browser("https://www.google.com");
}
