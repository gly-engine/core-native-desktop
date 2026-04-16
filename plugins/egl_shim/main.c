
/* ALi SoC display initialization via libeglplatform_shim.so.1 */

static void ali_display_init(uint16_t width, uint16_t height) {

    /* Set cfg_resolution env var — the shim reads it */

    char res[32];

    snprintf(res, sizeof(res), "%dx%d", width, height);

    setenv("cfg_resolution", res, 1);


    void *lib = dlopen("libeglplatform_shim.so.1", RTLD_LAZY);

    if (!lib) {

        /* Try absolute path */

        lib = dlopen("/usr/browser/netfront/libeglplatform_shim.so.1", RTLD_LAZY);

    }

    if (!lib) return;


    typedef int (*fn_shim_init)(void);

    fn_shim_init p_init = (fn_shim_init)dlsym(lib, "ShimInitialize");

    if (!p_init) { dlclose(lib); return; }


    p_init();


    /* Keep lib loaded — display state must persist */

}
