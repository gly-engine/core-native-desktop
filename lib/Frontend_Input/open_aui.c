#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "gedll.h"
#include "gecnd.h"

typedef void* aui_hdl;

typedef struct {
    /* 2 = IR */
    uint32_t n_key_code;

    /* 1=pressed, 2=released, 4=repeat */
    uint32_t e_status;

    uint32_t pad;

    /* NEC address/code */
    uint32_t n_key_adr;
} aui_key_info;

typedef void (*aui_key_cb)(aui_key_info*, void*);

typedef int (*PFN_aui_key_init)(void*, void*);
typedef int (*PFN_aui_key_open)(int, void*, aui_hdl*);
typedef int (*PFN_aui_key_callback_register)(aui_hdl, aui_key_cb);
typedef int (*PFN_aui_key_set_ir_rep_interval)(aui_hdl, int, int);

static void cb_keyboard(aui_key_info* ki, void* userdata)
{
    (void)userdata;

    if (!ki)
        return;

    if (ki->n_key_code != 2)
        return;

    if (ki->e_status == 1) {
        gecnd_set_btn_state(gecnd_get_root(), gecnd_nec_get_key(ki->n_key_adr), true);
    }

    if (ki->e_status == 2) {
        gecnd_set_btn_state(gecnd_get_root(), gecnd_nec_get_key(ki->n_key_adr), false);
    }
}

int gecnd_input_open_aui(void)
{
    LIB_HANDLE lib = load_library("libaui.so");
    if (!lib) {
        printf("Error: failed to load libaui.so\n");
        return -1;
    }

    PFN_aui_key_init aui_key_init =
        (PFN_aui_key_init)get_symbol(lib, "aui_key_init");

    PFN_aui_key_open aui_key_open =
        (PFN_aui_key_open)get_symbol(lib, "aui_key_open");

    PFN_aui_key_callback_register aui_key_callback_register =
        (PFN_aui_key_callback_register)get_symbol(lib, "aui_key_callback_register");

    PFN_aui_key_set_ir_rep_interval aui_key_set_ir_rep_interval =
        (PFN_aui_key_set_ir_rep_interval)get_symbol(lib, "aui_key_set_ir_rep_interval");

    if (!aui_key_init || !aui_key_open || !aui_key_callback_register) {
        printf("Error: failed to resolve required AUI symbols\n");
        close_library(lib);
        return -1;
    }

    if (aui_key_init(NULL, NULL) != 0) {
        printf("Error: aui_key_init failed\n");
        close_library(lib);
        return -1;
    }

    aui_hdl hdl = NULL;

    if (aui_key_open(0, NULL, &hdl) != 0) {
        printf("Error: aui_key_open failed\n");
        close_library(lib);
        return -1;
    }

    if (aui_key_set_ir_rep_interval) {
        aui_key_set_ir_rep_interval(hdl, 100, 100);
    }

    if (aui_key_callback_register(hdl, cb_keyboard) != 0) {
        printf("Error: aui_key_callback_register failed\n");
        close_library(lib);
        return -1;
    }

    return 0;
}
