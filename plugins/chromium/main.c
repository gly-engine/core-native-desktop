#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <uv.h>

#include "gecnd.h"

#define DEVTOOLS_RETRY       2000

static uv_process_t         proc;
static uv_process_options_t proc_opts;
static uv_timer_t           retry_timer;
static bool                 timer_active  = false;
static gly_req_id_t         ws_id         = 0;
static char                 http_body[4096];
static size_t               http_body_len = 0;

static void on_handle_close(uv_handle_t *handle) { (void)handle; }

static void on_ws_open (gly_req_id_t id, void *user) { 
    ws_id = id;
    uv_timer_stop(&retry_timer);
}

static void on_ws_msg  (gly_req_id_t id, const char *data, size_t len, void *user) {

}
static void on_ws_close(gly_req_id_t id, void *user) { 
    ws_id = 0;
}

static void on_ws_error(gly_req_id_t id, const char *msg,  void *user) {

}

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

static void on_http_status(gly_req_id_t id, int status, void *user) {
    http_body_len = 0;
}

static void on_http_error (gly_req_id_t id, const char *msg, void *user) {

}

static void try_connect_devtools(uv_timer_t *t) {
    if (ws_id) return;
    http_body_len = 0;
    gly_http_req_t req = {0};
    
    const char *devtools_url = getenv("chromium_url");
    
    if (!devtools_url) {
        devtools_url = "http://127.0.0.1:9222/json";
    }

    gamely_daemon_webclient_http(devtools_url, &req,
        on_http_status, on_http_data, on_http_done, on_http_error, NULL);
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
    if (!cmd) cmd   = default_chromium;

    const char *placeholder = strstr(cmd, "%s");
    if (!placeholder) return;

    size_t prefix_len = (size_t)(placeholder - cmd);
    size_t url_len    = strlen(url);
    size_t suffix_len = strlen(placeholder + 2);

    int slots = 1;
    for (const char *p = cmd; *p; p++)
        if (*p == ' ') slots++;

    char **args    = malloc((slots + 1) * sizeof(char *));
    if (!args) return;

    char  *expanded = malloc(prefix_len + url_len + suffix_len + 1);
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

}
