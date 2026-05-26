
#include "gecnd.h"

#include <string.h>
#include <stdio.h>

static char         g_url[512];
static gly_req_id_t g_ws_id      = 0;
static int          g_connecting = 0;
static int          g_cur_port   = 0;
static int          g_subscribed = 0;

static void try_connect(void);

static void on_open(gly_req_id_t id, void *user)
{
    (void)user;
    g_ws_id      = id;
    g_connecting = 0;
    g_cur_port   = 0;
    fprintf(stderr, "[core:input:remote] connected: %s\n", g_url);
}

static void on_msg(gly_req_id_t id, const char *data, size_t len, void *user)
{
    (void)id; (void)data; (void)len; (void)user;
}

static void on_close(gly_req_id_t id, void *user)
{
    (void)id; (void)user;
    fprintf(stderr, "[core:input:remote] disconnected: %s\n", g_url);
    g_ws_id      = 0;
    g_connecting = 0;
    try_connect();
}

static void on_error(gly_req_id_t id, const char *msg, void *user)
{
    (void)id; (void)user;
    fprintf(stderr, "[core:input:remote] error: %s\n", msg ? msg : "unknown");
    g_connecting = 0;
}

static void try_connect(void)
{
    if (!g_url[0] || g_ws_id || g_connecting) return;
    g_connecting = 1;
    gly_req_id_t id = gamely_daemon_webclient_ws_connect(g_url, "ws",
            on_open, on_msg, on_close, on_error, NULL);
    fprintf(stderr, "[core:input:remote] try_connect -> id=%u url=%s\n", id, g_url);
    if (!id) g_connecting = 0;
}

static void on_input(const char *name, bool pressed, int port, void *usr)
{
    (void)usr;
    if (!g_ws_id) { try_connect(); return; }

    char buf[32];
    int  len;

    if (port != g_cur_port) {
        len = snprintf(buf, sizeof(buf), "%d", port);
        gamely_daemon_webclient_ws_send(g_ws_id, buf, (size_t)len);
        g_cur_port = port;
    }

    len = snprintf(buf, sizeof(buf), "%c%s", pressed ? '+' : '-', name);
    gamely_daemon_webclient_ws_send(g_ws_id, buf, (size_t)len);
}

void gamely_daemon_input_remote(const char *url)
{
    if (!url || !url[0]) {
        g_url[0]     = '\0';
        g_connecting = 0;
        if (g_ws_id) gamely_daemon_webclient_ws_close(g_ws_id);
        return;
    }
    strncpy(g_url, url, sizeof(g_url) - 1);
    g_url[sizeof(g_url) - 1] = '\0';
    if (!g_subscribed) {
        gamely_input_add_cb("@code", on_input, NULL);
        g_subscribed = 1;
    }
    try_connect();
}
