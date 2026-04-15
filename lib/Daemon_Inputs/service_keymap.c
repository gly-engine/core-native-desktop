/**
 * @startuml
 * participant "set_toml" as T
 * participant "service_keymap" as KM
 * participant "driver" as D
 *
 * T  -> KM : add_class("vivensis.dtv30")
 * T  -> KM : add_keycode("up", 0xF3)
 * T  -> KM : add_keycode("a",  0xF1)
 * ...open()...
 * KM -> KM : parse URI — proto, classname, device, debug
 * KM -> KM : validate proto + classname
 * KM -> D  : open(port=0, device)
 * KM -> KM : free unused classes (if !debug)
 * @enduml
 */

#include "gamely_input.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* -- keyname bucket -- */

typedef struct {
    char (*pool)[8];
    int   count;
    int   capacity;
} gamely_keyname_bucket_t;

static const char *bucket_intern(gamely_keyname_bucket_t *b, const char *name)
{
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->pool[i], name) == 0)
            return b->pool[i];
    }
    if (b->count == b->capacity) {
        int nc = b->capacity ? b->capacity * 2 : 16;
        char (*np)[8] = realloc(b->pool, (size_t)nc * sizeof(*b->pool));
        if (!np) return NULL;
        b->pool = np;
        b->capacity = nc;
    }
    strncpy(b->pool[b->count], name, 7);
    b->pool[b->count][7] = '\0';
    return b->pool[b->count++];
}

/* -- keymap -- */

typedef struct { uint32_t code; const char *name; } gamely_keymap_entry_t;

typedef struct {
    gamely_keymap_entry_t *entries;
    int   count;
    int   capacity;
    char  name[32]; /* includes '\0'; max 31 useful chars */
    int   debug;
} gamely_keymap_t;

/* -- registry -- */

#define MAX_CLASSES 64

typedef struct {
    gamely_keymap_t       *classes[MAX_CLASSES];
    int                    count;
    gamely_keyname_bucket_t bucket;
    gamely_keymap_t       *current; /* class being built */
    gamely_keymap_t       *active;  /* selected by open() */
    char                   device[256];
    char                   proto[16];
    int                    debug;
    int                    port;
    const gamely_input_driver_t *driver;
} gamely_keymap_registry_t;

static gamely_keymap_registry_t g_reg;

/* -- driver table -- */

extern const gamely_input_driver_t gamely_driver_void;
extern const gamely_input_driver_t gamely_driver_lirc;
extern const gamely_input_driver_t gamely_driver_serial;
extern const gamely_input_driver_t gamely_driver_read;
extern const gamely_input_driver_t gamely_driver_aui;

static const struct { const char *proto; const gamely_input_driver_t *drv; } k_drivers[] = {
    {"void",   &gamely_driver_void},
    {"lirc",   &gamely_driver_lirc},
    {"serial", &gamely_driver_serial},
    {"read",   &gamely_driver_read},
    {"aui",    &gamely_driver_aui},
};
static const int k_driver_count = (int)(sizeof(k_drivers) / sizeof(k_drivers[0]));

/* -- build phase -- */

void gamely_daemon_input_add_class(const char *name)
{
    if (!name || strlen(name) >= 32 || g_reg.count >= MAX_CLASSES) return;

    gamely_keymap_t *km = calloc(1, sizeof(gamely_keymap_t));
    if (!km) return;

    strncpy(km->name, name, 31);
    km->name[31] = '\0';
    g_reg.classes[g_reg.count++] = km;
    g_reg.current = km;
}

void gamely_daemon_input_add_keycode(const char *key_name, uint32_t hex)
{
    if (!key_name || !g_reg.current) return;
    if (strlen(key_name) >= 8) {
        fprintf(stderr, "[core:input] key name too long (max 7): %s\n", key_name);
        return;
    }

    const char *interned = bucket_intern(&g_reg.bucket, key_name);
    if (!interned) return;

    gamely_keymap_t *km = g_reg.current;
    if (km->count == km->capacity) {
        int nc = km->capacity ? km->capacity * 2 : 8;
        gamely_keymap_entry_t *ne = realloc(km->entries, (size_t)nc * sizeof(*km->entries));
        if (!ne) return;
        km->entries = ne;
        km->capacity = nc;
    }

    /* insert sorted by code */
    int pos = km->count;
    while (pos > 0 && km->entries[pos - 1].code > hex) {
        km->entries[pos] = km->entries[pos - 1];
        pos--;
    }
    km->entries[pos].code = hex;
    km->entries[pos].name = interned;
    km->count++;
}

/* -- lookup -- */

const char *gamely_keymap_lookup(gamely_keymap_t *km, uint32_t code)
{
    if (!km || km->count == 0) return NULL;
    int lo = 0, hi = km->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if      (km->entries[mid].code == code) return km->entries[mid].name;
        else if (km->entries[mid].code <  code) lo = mid + 1;
        else                                    hi = mid - 1;
    }
    return NULL;
}

/* -- internal accessors used by service_io -- */

gamely_keymap_t *gamely_keymap_get_active(void) { return g_reg.active; }
int              gamely_keymap_get_debug(void)  { return g_reg.debug;  }
int              gamely_keymap_get_port(void)   { return g_reg.port;   }

const char *gamely_keymap_lookup_debug(uint32_t code, const char **out_class)
{
    for (int i = 0; i < g_reg.count; i++) {
        const char *name = gamely_keymap_lookup(g_reg.classes[i], code);
        if (name) {
            if (out_class) *out_class = g_reg.classes[i]->name;
            return name;
        }
    }
    if (out_class) *out_class = "?";
    return NULL;
}

/* -- activate -- */

bool gamely_daemon_input_open(const char *uri)
{
    if (!uri) return false;

    const char *sep = strstr(uri, "://");
    if (!sep) {
        fprintf(stderr, "[core:input] invalid URI: %s\n", uri);
        return false;
    }

    size_t plen = (size_t)(sep - uri);
    if (plen >= sizeof(g_reg.proto)) plen = sizeof(g_reg.proto) - 1;
    memcpy(g_reg.proto, uri, plen);
    g_reg.proto[plen] = '\0';

    /* find driver */
    g_reg.driver = NULL;
    for (int i = 0; i < k_driver_count; i++) {
        if (strcmp(k_drivers[i].proto, g_reg.proto) == 0) {
            g_reg.driver = k_drivers[i].drv;
            break;
        }
    }
    if (!g_reg.driver) {
        fprintf(stderr, "[core:input] unknown protocol: %s\n", g_reg.proto);
        return false;
    }

    /* parse classname */
    const char *rest   = sep + 3;
    const char *qmark  = strchr(rest, '?');
    char classname[32] = {0};
    size_t clen = qmark ? (size_t)(qmark - rest) : strlen(rest);
    if (clen >= sizeof(classname)) clen = sizeof(classname) - 1;
    memcpy(classname, rest, clen);

    /* parse query params */
    g_reg.device[0] = '\0';
    g_reg.debug     = 0;
    g_reg.port      = 0;

    if (qmark) {
        const char *p = qmark + 1;
        while (p && *p) {
            const char *amp = strchr(p, '&');
            size_t seg = amp ? (size_t)(amp - p) : strlen(p);
            if (strncmp(p, "device=", 7) == 0) {
                size_t vlen = seg - 7;
                if (vlen >= sizeof(g_reg.device)) vlen = sizeof(g_reg.device) - 1;
                memcpy(g_reg.device, p + 7, vlen);
                g_reg.device[vlen] = '\0';
            } else if (strncmp(p, "debug=", 6) == 0) {
                g_reg.debug = (p[6] == '1');
            }
            p = amp ? amp + 1 : NULL;
        }
    }

    /* find class ("0" = no class) */
    g_reg.active = NULL;
    if (strcmp(classname, "0") != 0) {
        for (int i = 0; i < g_reg.count; i++) {
            if (strcmp(g_reg.classes[i]->name, classname) == 0) {
                g_reg.active = g_reg.classes[i];
                break;
            }
        }
        if (!g_reg.active) {
            fprintf(stderr, "[core:input] class not found: %s\n", classname);
            return false;
        }
    }

    /* open driver */
    if (!g_reg.driver->open(g_reg.port, g_reg.device[0] ? g_reg.device : NULL)) {
        fprintf(stderr, "[core:input] driver open failed: %s\n", g_reg.proto);
        return false;
    }

    /* free unused classes if not debug */
    if (!g_reg.debug) {
        for (int i = 0; i < g_reg.count; i++) {
            if (g_reg.classes[i] != g_reg.active) {
                free(g_reg.classes[i]->entries);
                free(g_reg.classes[i]);
                g_reg.classes[i] = NULL;
            }
        }
        if (g_reg.active) {
            g_reg.classes[0] = g_reg.active;
            g_reg.count = 1;
        } else {
            g_reg.count = 0;
        }
    }

    return true;
}

void gamely_daemon_input_close(void)
{
    if (g_reg.driver)
        g_reg.driver->close(g_reg.port);

    for (int i = 0; i < g_reg.count; i++) {
        if (g_reg.classes[i]) {
            free(g_reg.classes[i]->entries);
            free(g_reg.classes[i]);
        }
    }
    free(g_reg.bucket.pool);
    memset(&g_reg, 0, sizeof(g_reg));
}

void gamely_daemon_input_init_keys(gamely_input_key_cb cb, void *usr)
{
    if (!cb || !g_reg.active) return;
    for (int i = 0; i < g_reg.active->count; i++)
        cb(g_reg.active->entries[i].name, false, 0, usr);
}
