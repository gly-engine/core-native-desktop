#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "gedll.h"
#include "gecnd.h"
#include "gehook.h"
#include "genec.h"

typedef struct aui_key_info {
    unsigned int n_key_code;
    unsigned int e_status;
    unsigned int n_key_addr;
} aui_key_info;

typedef void (*aui_key_callback)(aui_key_info *p_key_info, void *pv_user_data);

typedef struct {
    int (*aui_key_init)(void *pv_attr, void *pv_res);
    int (*aui_key_open)(unsigned int n_key_id, void *pv_attr, void **pp_handle_key);
    int (*aui_key_close)(void *p_handle_key);
    int (*aui_key_callback_register)(void *p_handle_key, aui_key_callback callback);
    int (*aui_key_set_ir_rep_interval)(void *p_handle_key, unsigned int n_delay, unsigned int n_interval);
} aui_api;

static aui_api ALI;
static bool g_ali_lib_loaded = false;
static LIB_HANDLE libaui = NULL;
static void *g_ali_hdl = NULL;

#define LOAD_SYM(lib, sym) \
    do { \
        *(void**)&ALI.sym = get_symbol(lib, #sym); \
        if (!ALI.sym) return false; \
    } while (0)

#define V_KEY_UP      0x01
#define V_KEY_DOWN    0x02
#define V_KEY_LEFT    0x03
#define V_KEY_RIGHT   0x04
#define V_KEY_ENTER   0x05
#define V_KEY_MENU    0x06
#define V_KEY_EXIT    0x07
#define V_KEY_RED     0x08
#define V_KEY_GREEN   0x09
#define V_KEY_YELLOW  0x0A
#define V_KEY_BLUE    0x0B

static const struct {
    unsigned int key;
    const char *name;
} keymap[] = {
    { V_KEY_UP,      "up" },
    { V_KEY_DOWN,    "down" },
    { V_KEY_LEFT,    "left" },
    { V_KEY_RIGHT,   "right" },
    { V_KEY_ENTER,   "a" },
    { V_KEY_RED,     "a" },
    { V_KEY_GREEN,   "b" },
    { V_KEY_YELLOW,  "c" },
    { V_KEY_BLUE,    "d" },
    { V_KEY_MENU,    "menu" },
    { V_KEY_EXIT,    "exit" },
};

#define KEY_COUNT (sizeof(keymap) / sizeof(keymap[0]))
#define KEY_TIMEOUT 3

static uint8_t g_ali_timeout_limit[KEY_COUNT];
static unsigned int g_ali_last_keycode = 0;
static bool g_ali_new_event = false;
static bool g_ali_inited = false;

static void ali_key_callback(aui_key_info *ki, void *pv_user_data) {
    (void)pv_user_data;
    if (ki->e_status == 1 || ki->e_status == 4) {
        g_ali_last_keycode = ki->n_key_code;
        g_ali_new_event = true;
    }
}

bool ali_load_aui(void) {
    if (g_ali_lib_loaded) return true;
    libaui = load_library("libaui.so");
    if (!libaui) return false;
    LOAD_SYM(libaui, aui_key_init);
    LOAD_SYM(libaui, aui_key_open);
    LOAD_SYM(libaui, aui_key_close);
    LOAD_SYM(libaui, aui_key_callback_register);
    LOAD_SYM(libaui, aui_key_set_ir_rep_interval);
    if (ALI.aui_key_init(NULL, NULL) != 0) return false;
    if (ALI.aui_key_open(0, NULL, &g_ali_hdl) != 0) return false;
    ALI.aui_key_set_ir_rep_interval(g_ali_hdl, 100, 100);
    if (ALI.aui_key_callback_register(g_ali_hdl, ali_key_callback) != 0) return false;
    g_ali_lib_loaded = true;
    return true;
}

void gly_hook_keyboard_has_media(bool *has_media) {
    *has_media = true;
}

void gly_hook_input_keyboard(uint8_t idx, char **key, bool *pressed) {
    if (!g_ali_inited) {
        ali_load_aui();
        g_ali_inited = true;
    }
    if (!g_ali_lib_loaded) {
        *key = NULL;
        *pressed = false;
        return;
    }
    static unsigned int current_keycode = 0;
    if (idx == 0) {
        if (g_ali_new_event) {
            current_keycode = g_ali_last_keycode;
            g_ali_new_event = false;
        } else {
            current_keycode = 0;
        }
    }
    if (idx < KEY_COUNT) {
        if (keymap[idx].key == current_keycode) {
            if (g_ali_timeout_limit[idx] == 0) *key = (char*)keymap[idx].name;
            g_ali_timeout_limit[idx] = KEY_TIMEOUT;
            *pressed = true;
            return;
        }
        if (g_ali_timeout_limit[idx] > 0) {
            if (--g_ali_timeout_limit[idx] == 0) {
                *key = (char*)keymap[idx].name;
                *pressed = false;
                return;
            }
        }
        *key = NULL;
        *pressed = true;
    } else {
        *key = NULL;
        *pressed = false;
    }
}
