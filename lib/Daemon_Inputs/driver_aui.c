#include "gamely_input.h"
#include "gedll.h"

#include <stdio.h>
#include <stdint.h>

typedef void *aui_hdl;

typedef struct {
    uint32_t n_key_code; /* 2 = IR */
    uint32_t e_status;   /* 1=pressed 2=released 4=repeat */
    uint32_t pad;
    uint32_t n_key_adr;  /* NEC address/code */
} aui_key_info;

typedef void (*aui_key_cb)(aui_key_info *, void *);

typedef int (*PFN_aui_key_init)(void *, void *);
typedef int (*PFN_aui_key_open)(int, void *, aui_hdl *);
typedef int (*PFN_aui_key_callback_register)(aui_hdl, aui_key_cb);
typedef int (*PFN_aui_key_set_ir_rep_interval)(aui_hdl, int, int);

static void on_aui_key(aui_key_info *ki, void *userdata)
{
    (void)userdata;
    if (!ki || ki->n_key_code != 2) return;
    if (ki->e_status == 1) gamely_daemon_input_push(ki->n_key_adr, true,  0);
    if (ki->e_status == 2) gamely_daemon_input_push(ki->n_key_adr, false, 0);
}

static LIB_HANDLE g_lib = NULL;
static aui_hdl    g_hdl = NULL;

static bool aui_open(int port, const char *device)
{
    (void)port; (void)device;

    g_lib = load_library("libaui.so");
    if (!g_lib) { fprintf(stderr, "[core:input:aui] libaui.so not found\n"); return false; }

    PFN_aui_key_init              aui_key_init              = get_symbol(g_lib, "aui_key_init");
    PFN_aui_key_open              aui_key_open              = get_symbol(g_lib, "aui_key_open");
    PFN_aui_key_callback_register aui_key_callback_register = get_symbol(g_lib, "aui_key_callback_register");
    PFN_aui_key_set_ir_rep_interval aui_set_interval        = get_symbol(g_lib, "aui_key_set_ir_rep_interval");

    if (!aui_key_init || !aui_key_open || !aui_key_callback_register) {
        fprintf(stderr, "[core:input:aui] missing symbols\n");
        close_library(g_lib); g_lib = NULL; return false;
    }
    if (aui_key_init(NULL, NULL) != 0) {
        fprintf(stderr, "[core:input:aui] init failed\n");
        close_library(g_lib); g_lib = NULL; return false;
    }
    if (aui_key_open(0, NULL, &g_hdl) != 0) {
        fprintf(stderr, "[core:input:aui] open failed\n");
        close_library(g_lib); g_lib = NULL; return false;
    }
    if (aui_set_interval) aui_set_interval(g_hdl, 100, 100);
    if (aui_key_callback_register(g_hdl, on_aui_key) != 0) {
        fprintf(stderr, "[core:input:aui] callback register failed\n");
        close_library(g_lib); g_lib = NULL; return false;
    }
    return true;
}

static void aui_close(int port)
{
    (void)port;
    if (g_lib) { close_library(g_lib); g_lib = NULL; }
    g_hdl = NULL;
}

const gamely_input_driver_t gamely_driver_aui = { aui_open, aui_close };
