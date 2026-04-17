#include "gecnd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

typedef int (*fn_shim_init)(void);
   
void coreopen_shim_gecnd() {
    char *err = NULL;
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
        err = dlerror();
        if (!lib) {
            err = err? err: "unknown error";
            snprintf(buffer, sizeof(buffer), "dlopen('%s') failed: %s", path, err);
            break;
        }

        fn_shim_init p_init = (fn_shim_init)dlsym(lib, "ShimInitialize");
        err = dlerror();

        if (err || !p_init) {
            err = err? err: "undefined symbol";
            snprintf(buffer, sizeof(buffer), "dlsym('ShimInitialize') failed: %s", err);
            break;
        }

        p_init();

        status = 200;
        snprintf(buffer, sizeof(buffer), "success start hdmi: %dx%d", width, height);

    } while (0);

    if (err && lib) {
        dlclose(lib);
    }
}
