#include "gecnd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

typedef int (*fn_shim_init)(void);

static void callback(const gly_http_req_t *req) {
    void *lib = NULL;
    int status = 500;
    char buffer[256];

    do {
        const char *path = "/usr/browser/netfront/libeglplatform_shim.so.1";
        int width = 1920;
        int height = 1080;

        snprintf(buffer, sizeof(buffer), "%dx%d", width, height);
        setenv("cfg_resolution", buffer, 1);

        lib = dlopen(path, RTLD_LAZY);
        if (!lib) {
            const char *err = dlerror();
            snprintf(buffer, sizeof(buffer), "dlopen('%s') failed: %s", path, err ? err : "unknown error");
            break;
        }

        dlerror();
        fn_shim_init p_init = (fn_shim_init)dlsym(lib, "ShimInitialize");

        const char *err = dlerror();
        if (err) {
            snprintf(buffer, sizeof(buffer), "dlsym('ShimInitialize') failed: %s", err);
            break;
        }

        p_init();

        status = 200;
        snprintf(buffer, sizeof(buffer), "success start hdmi: %dx%d", width, height);

    } while (0);

    if (lib) {
        dlclose(lib);
    }

    gamely_daemon_webserver_http_send(req->id, status, "text/plain; charset=utf-8", buffer, strlen(buffer));
}

void init() {
    gamely_daemon_webloop_route_http("/plugin/shim", callback);
}
