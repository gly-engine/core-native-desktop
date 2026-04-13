#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <uv.h>

/*
#include <picohttpparser.h>

#include "gecnd.h"
#include "gehook.h"

#define WS_PORT          9222
#define WS_HOST          "127.0.0.1"
#define WS_RETRY_MS      2000
#define HTTP_BUF_SIZE    8192
#define WS_PATH_SIZE     256
#define ARG_BUF_SIZE     256
#define HANDSHAKE_SIZE   512
#define CMD_BUF_SIZE     512
#define FRAME_BUF_SIZE   1024
#define MAX_ARGS         32
#define MAX_HTTP_HEADERS 32

static uv_process_t         proc;
static uv_process_options_t proc_opts;
static char                *proc_args[MAX_ARGS];
static int                  proc_argc;

static uv_tcp_t     ws_client;
static uv_connect_t ws_connect_req;
static uv_timer_t   ws_timer;
static bool          ws_connected;
static char          ws_path[WS_PATH_SIZE];

static uv_tcp_t     http_client;
static uv_connect_t http_connect_req;
static bool          http_in_progress;
static char          http_buf[HTTP_BUF_SIZE];
static size_t        http_len;

static void on_close(uv_handle_t *h) {
    printf("[WS] close\n");
    if (h == (uv_handle_t *)&ws_client)
        ws_connected = false;
}

static void on_write(uv_write_t *req, int status) {
    printf("[WS] write status=%d\n", status);
    free(req);
}

static void alloc_cb(uv_handle_t *h, size_t suggested, uv_buf_t *buf) {
    buf->base = malloc(suggested);
    buf->len  = suggested;
}

static uv_write_t *write_alloc(void) {
    return malloc(sizeof(uv_write_t));
}

static void stream_write(uv_stream_t *s, const char *data, size_t len, uv_write_cb cb) {
    uv_buf_t buf    = uv_buf_init((char *)data, len);
    uv_write_t *req = write_alloc();
    uv_write(req, s, &buf, 1, cb);
}

static void arg_reset(void) {
    for (int i = 0; i < proc_argc; i++) {
        if (proc_args[i]) {
            free(proc_args[i]);
            proc_args[i] = NULL;
        }
    }
    proc_argc = 0;
}

static void arg_push(const char *s) {
    if (proc_argc < MAX_ARGS - 1)
        proc_args[proc_argc++] = strdup(s);
}

static void arg_push_kv(const char *k, const char *v) {
    char tmp[ARG_BUF_SIZE];
    snprintf(tmp, sizeof(tmp), "%s=%s", k, v);
    arg_push(tmp);
}

static void arg_finalize(void) {
    proc_args[proc_argc] = NULL;
}

static void extract_ws_path(const char *json) {
    const char *p = strstr(json, "webSocketDebuggerUrl");
    if (!p) return;

    p = strstr(p, "ws://");
    if (!p) return;

    p = strchr(p + 5, '/');
    if (!p) return;

    const char *end = strchr(p, '"');
    if (!end) return;

    size_t len = end - p;
    if (len >= WS_PATH_SIZE)
        len = WS_PATH_SIZE - 1;

    memcpy(ws_path, p, len);
    ws_path[len] = '\0';

    printf("[WS] discovered path: %s\n", ws_path);
}

static void http_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    if (nread > 0) {
        if (http_len + (size_t)nread >= HTTP_BUF_SIZE)
            http_len = 0;

        memcpy(http_buf + http_len, buf->base, nread);
        http_len += nread;

        struct phr_header headers[MAX_HTTP_HEADERS];
        size_t num_headers = MAX_HTTP_HEADERS;
        int minor, status;
        const char *msg;
        size_t msg_len;

        int ret = phr_parse_response(http_buf, http_len,
            &minor, &status, &msg, &msg_len,
            headers, &num_headers, 0);

        if (ret > 0) {
            printf("[HTTP] status %d\n", status);
            printf("[HTTP BODY]\n%.*s\n", (int)(http_len - ret), http_buf + ret);
            extract_ws_path(http_buf + ret);
            http_in_progress = false;
            uv_close((uv_handle_t *)stream, NULL);
        } else if (ret == -1) {
            printf("[HTTP] parse error\n");
            http_in_progress = false;
            uv_close((uv_handle_t *)stream, NULL);
        }
    }

    if (nread < 0) {
        printf("[HTTP] read error: %s\n", uv_strerror(nread));
        http_in_progress = false;
        uv_close((uv_handle_t *)stream, NULL);
    }

    free(buf->base);
}

static void http_on_connect(uv_connect_t *req, int status) {
    if (status < 0) {
        printf("[HTTP] connect error: %s\n", uv_strerror(status));
        http_in_progress = false;
        return;
    }

    static const char request[] =
        "GET /json HTTP/1.1\r\n"
        "Host: " WS_HOST ":9222\r\n"
        "Connection: close\r\n"
        "\r\n";

    stream_write((uv_stream_t *)&http_client, request, sizeof(request) - 1, on_write);
    uv_read_start((uv_stream_t *)&http_client, alloc_cb, http_read_cb);
}

static void http_get_devtools(uv_loop_t *loop) {
    http_len = 0;

    uv_tcp_init(loop, &http_client);

    struct sockaddr_in addr;
    uv_ip4_addr(WS_HOST, WS_PORT, &addr);

    uv_tcp_connect(&http_connect_req, &http_client,
        (const struct sockaddr *)&addr, http_on_connect);
}

static void ws_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    if (nread > 0) {
        printf("[WS] recv %ld bytes\n", nread);
        fwrite(buf->base, 1, nread, stdout);
        printf("\n");
    }
    if (nread < 0) {
        printf("[WS] read error: %s\n", uv_strerror(nread));
        uv_close((uv_handle_t *)stream, on_close);
    }
    free(buf->base);
}

static void ws_on_connect(uv_connect_t *req, int status) {
    printf("[WS] connect status=%d\n", status);

    if (status < 0) {
        printf("[WS] connect error: %s\n", uv_strerror(status));
        return;
    }

    ws_connected = true;

    char handshake[HANDSHAKE_SIZE];
    int len = snprintf(handshake, sizeof(handshake),
        "GET %s HTTP/1.1\r\n"
        "Host: " WS_HOST ":9222\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        ws_path);

    printf("[WS] handshake path: %s\n", ws_path);

    stream_write((uv_stream_t *)&ws_client, handshake, len, on_write);
    uv_read_start((uv_stream_t *)&ws_client, alloc_cb, ws_read_cb);
}

static void ws_try_connect(uv_timer_t *t) {
    if (ws_connected) {
        printf("[WS] already connected\n");
        return;
    }

    if (ws_path[0] == '\0') {
        if (http_in_progress) {
            printf("[HTTP] request already running\n");
            return;
        }
        printf("[HTTP] requesting /json\n");
        http_in_progress = true;
        http_get_devtools(t->loop);
        return;
    }

    printf("[WS] connecting websocket path=%s\n", ws_path);

    uv_tcp_init(t->loop, &ws_client);

    struct sockaddr_in addr;
    uv_ip4_addr(WS_HOST, WS_PORT, &addr);

    uv_tcp_connect(&ws_connect_req, &ws_client,
        (const struct sockaddr *)&addr, ws_on_connect);
}

static void send_ws_cmd(const char *method, const char *params) {
    if (!ws_connected) {
        printf("[WS] not connected\n");
        return;
    }

    char msg[CMD_BUF_SIZE];
    int len = snprintf(msg, sizeof(msg),
        "{\"id\":1,\"method\":\"%s\",\"params\":{%s}}", method, params);

    printf("[WS] send cmd: %s\n", msg);

    static const uint8_t mask[4] = {1, 2, 3, 4};
    unsigned char frame[FRAME_BUF_SIZE];
    int p = 0;

    frame[p++] = 0x81;

    if (len <= 125) {
        frame[p++] = 0x80 | len;
    } else {
        frame[p++] = 0x80 | 126;
        frame[p++] = (len >> 8) & 0xFF;
        frame[p++] = len & 0xFF;
    }

    memcpy(frame + p, mask, 4);
    p += 4;

    for (int i = 0; i < len; i++)
        frame[p++] = msg[i] ^ mask[i % 4];

    stream_write((uv_stream_t *)&ws_client, (char *)frame, p, on_write);
}

void native_browser_url(const char *url) {
    gecnd_t *gly = gecnd_get_root();
    if (!gly || (gly->internal & GECND_INTERNAL_BROWSER))
        return;

    printf("[Browser] open url: %s\n", url);

    char res[32];
    snprintf(res, sizeof(res), "%dx%d", gly->width, gly->height);

    arg_reset();
    arg_push(gly->browser_bin ? gly->browser_bin : "/usr/browser/netfront/cefsimple");
    arg_push_kv("--url", url);

#ifdef __arm__
    arg_push("--no-sandbox");
    arg_push("--no-zygote");
    arg_push_kv("--ozone-platform", "egltest");
    arg_push_kv("--content-shell-host-window-size", res);
    arg_push("--disable-es3-gl-context");
    arg_push_kv("--use-gl", "egl");
    arg_push("--ignore-gpu-blocklist");
    arg_push("--ignore-certificate-errors");
    arg_push("--disable-gpu-rasterization");
    arg_push("--disable-dev-shm-usage");
    arg_push("--enable-transparency");
#else
    arg_push_kv("--window-size", res);
    arg_push("--remote-allow-origins=*");
#endif

    arg_push_kv("--remote-debugging-port", "9222");
    arg_finalize();

    for (int j = 0; j < proc_argc; j++)
        printf("[Browser arg] %s\n", proc_args[j]);

    uv_stdio_container_t child_stdio[3] = {
        { .flags = UV_IGNORE },
        { .flags = UV_IGNORE },
        { .flags = UV_IGNORE },
    };

    memset(&proc_opts, 0, sizeof(proc_opts));
    proc_opts.exit_cb     = NULL;
    proc_opts.file        = proc_args[0];
    proc_opts.args        = proc_args;
    proc_opts.flags       = UV_PROCESS_DETACHED;
    proc_opts.stdio_count = 3;
    proc_opts.stdio       = child_stdio;

    uv_loop_t *loop = (uv_loop_t *)gly->loop;
    int r = uv_spawn(loop, &proc, &proc_opts);

    printf("[Browser] spawn result=%d\n", r);

    if (r != 0) {
        printf("[Browser] spawn error: %s\n", uv_strerror(r));
        return;
    }

    printf("[Browser] pid=%d\n", proc.pid);

    uv_unref((uv_handle_t *)&proc);
    gly->internal |= GECND_INTERNAL_BROWSER;

    ws_connected    = false;
    ws_path[0]      = '\0';
    http_in_progress = false;
    http_len         = 0;

    uv_timer_init(loop, &ws_timer);
    uv_timer_start(&ws_timer, ws_try_connect, WS_RETRY_MS, WS_RETRY_MS);
}

void native_browser_exit(void) {
    gecnd_t *gly = gecnd_get_root();
    if (!gly || !(gly->internal & GECND_INTERNAL_BROWSER))
        return;

    printf("[Browser] exit\n");

    uv_timer_stop(&ws_timer);
    uv_close((uv_handle_t *)&ws_timer, NULL);

    uv_process_kill(&proc, SIGTERM);
    uv_close((uv_handle_t *)&proc, NULL);

    if (ws_connected)
        uv_close((uv_handle_t *)&ws_client, on_close);

    gly->internal &= ~GECND_INTERNAL_BROWSER;
    arg_reset();
}

void open_cef_key_handler(gecnd_key_t key, bool pressed) {
    printf("[KEY] key=%d pressed=%d\n", key, pressed);

    if (!ws_connected) {
        printf("[KEY] websocket not connected\n");
        return;
    }

    const char *k;
    int code;

    switch (key) {
    case GECND_KEY_A:     k = "Enter";      code = 13; break;
    case GECND_KEY_UP:    k = "ArrowUp";     code = 38; break;
    case GECND_KEY_DOWN:  k = "ArrowDown";   code = 40; break;
    case GECND_KEY_LEFT:  k = "ArrowLeft";   code = 37; break;
    case GECND_KEY_RIGHT: k = "ArrowRight";  code = 39; break;
    case GECND_KEY_MENU:  k = "Backspace";   code = 8;  break;
    default:
        printf("[KEY] unmapped\n");
        return;
    }

    printf("[KEY] mapped key=%s code=%d\n", k, code);

    char params[ARG_BUF_SIZE];
    snprintf(params, sizeof(params),
        "\"type\":\"%s\","
        "\"windowsVirtualKeyCode\":%d,"
        "\"key\":\"%s\","
        "\"code\":\"%s\"",
        pressed ? "keyDown" : "keyUp", code, k, k);

    send_ws_cmd("Input.dispatchKeyEvent", params);
}
*/