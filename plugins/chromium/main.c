#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <lauxlib.h>
#include <lua.h>
#include <uv.h>

#include "gecnd.h"

#define DEVTOOLS_RETRY     500
#define DEVTOOLS_RETRY_MAX 5
#define DEVTOOLS_HOST      "127.0.0.1"
#define DEVTOOLS_PORT      9222
#define DEVTOOLS_WS_PATH   "/api/dev-tools"

typedef struct { const char *engine; const char *cdp_key; const char *cdp_code; int vk; } key_entry_t;

static const key_entry_t key_map[] = {
    { "a",     "Enter",      "Enter",      13 },
    { "down",  "ArrowDown",  "ArrowDown",  40 },
    { "left",  "ArrowLeft",  "ArrowLeft",  37 },
    { "menu",  "Backspace",  "Backspace",   8 },
    { "right", "ArrowRight", "ArrowRight", 39 },
    { "up",    "ArrowUp",    "ArrowUp",    38 },
};

static int key_map_cmp(const void *key, const void *entry) {
    return strcmp((const char *)key, ((const key_entry_t *)entry)->engine);
}

static uv_process_t         proc;
static uv_process_options_t proc_opts;
static uv_timer_t           retry_timer;
static bool                 timer_active  = false;
static bool                 http_pending  = false;
static bool                 ws_connecting = false;
static gly_req_id_t         ws_id         = 0;
static char                 http_buf[4096];
static size_t               http_buf_len  = 0;
static uint32_t             cdp_msg_id    = 0;
static char                 ws_path[256]  = {0};
static int                  ws_fail_count = 0;

static void on_handle_close(uv_handle_t *handle) { (void)handle; }
static void try_connect_devtools(uv_timer_t *t);

static void on_ws_open(gly_req_id_t id, void *user) {
    ws_id         = id;
    ws_connecting = false;
    ws_fail_count = 0;
    uv_timer_stop(&retry_timer);
}

static void on_ws_msg(gly_req_id_t id, const char *data, size_t len, void *user) {
    (void)id; (void)user;
    gamely_daemon_webserver_ws_send_all(DEVTOOLS_WS_PATH, data, len, 0);
}

static void ws_dev_tools(const gly_ws_req_t *req) {
    if (req->event != GLY_WS_MESSAGE || !ws_id) return;
    gamely_daemon_webclient_ws_send(ws_id, req->data, req->len);
}

static void on_ws_close(gly_req_id_t id, void *user) {
    ws_id         = 0;
    ws_connecting = false;
    if (timer_active)
        uv_timer_start(&retry_timer, try_connect_devtools, DEVTOOLS_RETRY, DEVTOOLS_RETRY);
}

static void on_ws_error(gly_req_id_t id, const char *msg, void *user) {
    ws_connecting = false;
    if (ws_id != id) return;
    ws_id = 0;
    if (++ws_fail_count >= DEVTOOLS_RETRY_MAX) {
        ws_path[0]    = '\0';
        ws_fail_count = 0;
    }
    if (timer_active)
        uv_timer_start(&retry_timer, try_connect_devtools, DEVTOOLS_RETRY, DEVTOOLS_RETRY);
}

static void on_key(const char *name, bool pressed, int port, void *usr) {
    (void)port; (void)usr;
    if (!ws_id) return;

    const key_entry_t *e = bsearch(name, key_map,
        sizeof(key_map)/sizeof(key_map[0]), sizeof(key_map[0]), key_map_cmp);
    if (!e) return;

    char buf[256];
    int  len = snprintf(buf, sizeof(buf),
        "{\"id\":%u,\"method\":\"Input.dispatchKeyEvent\","
        "\"params\":{\"type\":\"%s\",\"key\":\"%s\","
        "\"code\":\"%s\",\"windowsVirtualKeyCode\":%d,"
        "\"nativeVirtualKeyCode\":%d}}",
        ++cdp_msg_id,
        pressed ? "rawKeyDown" : "keyUp",
        e->cdp_key, e->cdp_code, e->vk, e->vk);

    gamely_daemon_webclient_ws_send(ws_id, buf, (size_t)len);
}

static void on_http_done(gly_req_id_t id, void *user) {
    http_pending = false;

    const char *p = strstr(http_buf, "webSocketDebuggerUrl");
    if (!p) return;
    p = strstr(p, "ws://");
    if (!p) return;
    p = strchr(p + 5, '/');
    if (!p) return;
    const char *end = strchr(p, '"');
    if (!end) return;

    size_t len = (size_t)(end - p);
    if (len >= sizeof(ws_path)) return;
    memcpy(ws_path, p, len);
    ws_path[len] = '\0';
}

static void on_http_data(gly_req_id_t id, const char *data, size_t len, void *user) {
    if (http_buf_len + len < sizeof(http_buf)) {
        memcpy(http_buf + http_buf_len, data, len);
        http_buf_len += len;
        http_buf[http_buf_len] = '\0';
    }
}

static void on_http_status(gly_req_id_t id, int status, void *user) {
    http_buf_len = 0;
}

static void on_http_error(gly_req_id_t id, const char *msg, void *user) {
    http_pending = false;
}

static void try_connect_devtools(uv_timer_t *t) {
    if (ws_id || ws_connecting) return;

    if (ws_path[0] != '\0') {
        char ws_url[320];
        snprintf(ws_url, sizeof(ws_url),
            "ws://" DEVTOOLS_HOST ":%d%s", DEVTOOLS_PORT, ws_path);
        ws_connecting = true;
        if (!gamely_daemon_webclient_ws_connect(ws_url, NULL,
                on_ws_open, on_ws_msg, on_ws_close, on_ws_error, NULL))
            ws_connecting = false;
        return;
    }

    if (http_pending) return;

    const char *url = getenv("chromium_url");
    if (!url) url = "http://" DEVTOOLS_HOST ":9222/json";

    http_pending  = true;
    http_buf_len  = 0;
    gly_http_req_t req = {0};
    if (!gamely_daemon_webclient_http(url, &req,
            on_http_status, on_http_data, on_http_done, on_http_error, NULL))
        http_pending = false;
}

static void on_browser_exit(uv_process_t *p, int64_t status, int signal) {
    gecnd_t *gly = gecnd_get_root();
    if (gly) gly->internal &= ~GECND_INTERNAL_BROWSER;

    if (ws_id) {
        gamely_daemon_webclient_ws_close(ws_id);
        ws_id = 0;
    }

    if (timer_active) {
        uv_timer_stop(&retry_timer);
        uv_close((uv_handle_t *)&retry_timer, on_handle_close);
        timer_active = false;
    }

    ws_path[0]    = '\0';
    http_pending  = false;
    ws_connecting = false;
    ws_fail_count = 0;

    uv_close((uv_handle_t *)p, on_handle_close);
}

static void native_browser_exit(void) {
    gecnd_t *gly = gecnd_get_root();
    if (!gly || !(gly->internal & GECND_INTERNAL_BROWSER)) return;
    uv_process_kill(&proc, SIGTERM);
}

static const char default_chromium[] = "chromium %s --remote-debugging-port=9222 --remote-allow-origins=*";

static void native_browser_url(const char *url) {
    gecnd_t *gly = gecnd_get_root();
    if (!gly || (gly->internal & GECND_INTERNAL_BROWSER) || !url) return;

    const char *cmd = getenv("chromium");
    if (!cmd) cmd = default_chromium;

    const char *placeholder = strstr(cmd, "%s");
    if (!placeholder) return;

    size_t prefix_len = (size_t)(placeholder - cmd);
    size_t url_len    = strlen(url);
    size_t suffix_len = strlen(placeholder + 2);

    int slots = 1;
    for (const char *p = cmd; *p; p++)
        if (*p == ' ') slots++;

    char **args = malloc((slots + 1) * sizeof(char *));
    if (!args) return;

    char *expanded = malloc(prefix_len + url_len + suffix_len + 1);
    if (!expanded) { free(args); return; }

    memcpy(expanded,                        cmd,             prefix_len);
    memcpy(expanded + prefix_len,           url,             url_len);
    memcpy(expanded + prefix_len + url_len, placeholder + 2, suffix_len + 1);

    int argc = 0;
    for (char *tok = strtok(expanded, " "); tok; tok = strtok(NULL, " "))
        args[argc++] = tok;
    args[argc] = NULL;

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

    uv_loop_t *loop    = gly->loop;
    int        spawned = uv_spawn(loop, &proc, &proc_opts);
    free(expanded);
    free(args);
    if (spawned != 0) return;

    gly->internal |= GECND_INTERNAL_BROWSER;
    ws_path[0]    = '\0';
    http_pending  = false;
    ws_connecting = false;
    ws_id         = 0;

    uv_timer_init(loop, &retry_timer);
    uv_timer_start(&retry_timer, try_connect_devtools, DEVTOOLS_RETRY, DEVTOOLS_RETRY);
    timer_active = true;
}

static int lua_native_browser_url(lua_State *L) {
    const char *url = lua_tostring(L, 1);
    native_browser_url(url);
    return 0;
}

static int lua_native_browser_exit(lua_State *L) {
    (void)L;
    native_browser_exit();
    return 0;
}

int luaopen_chromium_gecnd(lua_State *L) {
    const luaL_Reg api[] = {
        { "native_browser_url",  lua_native_browser_url  },
        { "native_browser_exit", lua_native_browser_exit },
        { NULL, NULL }
    };

    for (int i = 0; api[i].name; i++) {
        lua_register(L, api[i].name, api[i].func);
    }
    
    return 0;
}

void coreopen_chromium_gecnd(void) {
    gamely_daemon_input_subscribe(on_key, NULL);
    gamely_daemon_webloop_route_ws(DEVTOOLS_WS_PATH, ws_dev_tools);
}
