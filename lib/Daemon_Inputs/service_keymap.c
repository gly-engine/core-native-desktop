/**
 * @startuml
 * participant "set_toml" as T
 * participant "service_keymap" as KM
 * participant "driver" as D
 *
 * T  -> KM : add_class("vivensis.dtv30")
 * T  -> KM : add_keycode("up", 0xF3)
 * T  -> KM : add_keycode("a",  0xF1)
 * ...add_source()...
 * KM -> KM : parse URI — proto, classname, device, port, debug
 * KM -> KM : store source slot
 * ...open()...
 * KM -> KM : validate proto + classname per source
 * KM -> D  : open(port, device) per source
 * KM -> KM : free unused classes (if no source has debug=1)
 * @enduml
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "gecnd.h"

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
    char  name[32];
    int   debug;
} gamely_keymap_t;

/* -- source -- */

#define MAX_SOURCES 8

typedef struct {
    char                         proto[16];
    char                         classname[32];
    char                         device[256];
    int                          port;
    int                          debug;
    const gamely_input_driver_t *driver;
    gamely_keymap_t             *active;
} gamely_input_source_t;

/* -- registry -- */

#define MAX_CLASSES 64

typedef struct {
    gamely_keymap_t         *classes[MAX_CLASSES];
    int                      count;
    gamely_keyname_bucket_t  bucket;
    gamely_keymap_t         *current;
    gamely_input_source_t    sources[MAX_SOURCES];
    int                      source_count;
} gamely_keymap_registry_t;

static gamely_keymap_registry_t g_reg;

/* -- driver table -- */

extern const gamely_input_driver_t gamely_driver_void;
extern const gamely_input_driver_t gamely_driver_lirc;
extern const gamely_input_driver_t gamely_driver_serial;
extern const gamely_input_driver_t gamely_driver_read;

static const struct { const char *proto; const gamely_input_driver_t *drv; } k_drivers[] = {
    {"void",   &gamely_driver_void},
    {"lirc",   &gamely_driver_lirc},
    {"serial", &gamely_driver_serial},
    {"read",   &gamely_driver_read}
};

static const int k_driver_count = (int)(sizeof(k_drivers) / sizeof(k_drivers[0]));

/* -- build phase -- */

void gamely_daemon_input_add_class(const char *name)
{
    if (!name || strlen(name) >= 32) return;

    for (int i = 0; i < g_reg.count; i++) {
        if (g_reg.classes[i] && strcmp(g_reg.classes[i]->name, name) == 0) {
            g_reg.current = g_reg.classes[i];
            return;
        }
    }

    if (g_reg.count >= MAX_CLASSES) return;

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

gamely_keymap_t *gamely_keymap_get_active(void) { return g_reg.source_count ? g_reg.sources[0].active : NULL; }
int              gamely_keymap_get_debug(void)  { return g_reg.source_count ? g_reg.sources[0].debug  : 0;    }
int              gamely_keymap_get_port(void)   { return g_reg.source_count ? g_reg.sources[0].port   : 0;    }

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

/* -- add source -- */

void gamely_daemon_input_add_source(const char *uri)
{
    if (!uri || g_reg.source_count >= MAX_SOURCES) return;

    gamely_input_source_t *src = &g_reg.sources[g_reg.source_count];
    memset(src, 0, sizeof(*src));

    const char *sep = strstr(uri, "://");
    if (!sep) {
        fprintf(stderr, "[core:input] invalid URI: %s\n", uri);
        return;
    }

    size_t plen = (size_t)(sep - uri);
    if (plen >= sizeof(src->proto)) plen = sizeof(src->proto) - 1;
    memcpy(src->proto, uri, plen);
    src->proto[plen] = '\0';

    for (int i = 0; i < k_driver_count; i++) {
        if (strcmp(k_drivers[i].proto, src->proto) == 0) {
            src->driver = k_drivers[i].drv;
            break;
        }
    }
    if (!src->driver) {
        fprintf(stderr, "[core:input] unknown protocol: %s\n", src->proto);
        return;
    }

    const char *rest  = sep + 3;
    const char *qmark = strchr(rest, '?');
    size_t clen = qmark ? (size_t)(qmark - rest) : strlen(rest);
    if (clen >= sizeof(src->classname)) clen = sizeof(src->classname) - 1;
    memcpy(src->classname, rest, clen);
    src->classname[clen] = '\0';

    if (qmark) {
        const char *p = qmark + 1;
        while (p && *p) {
            const char *amp = strchr(p, '&');
            size_t seg = amp ? (size_t)(amp - p) : strlen(p);
            if (strncmp(p, "device=", 7) == 0) {
                size_t vlen = seg - 7;
                if (vlen >= sizeof(src->device)) vlen = sizeof(src->device) - 1;
                memcpy(src->device, p + 7, vlen);
                src->device[vlen] = '\0';
            } else if (strncmp(p, "debug=", 6) == 0) {
                src->debug = (p[6] == '1');
            } else if (strncmp(p, "port=", 5) == 0) {
                src->port = (int)strtol(p + 5, NULL, 10);
            }
            p = amp ? amp + 1 : NULL;
        }
    }

    g_reg.source_count++;
}

/* -- activate -- */

bool gamely_daemon_input_open(void)
{
    if (g_reg.source_count == 0)
        gamely_daemon_input_add_source("void://0");

    int any_debug = 0;
    for (int i = 0; i < g_reg.source_count; i++)
        if (g_reg.sources[i].debug) any_debug = 1;

    for (int i = 0; i < g_reg.source_count; i++) {
        gamely_input_source_t *src = &g_reg.sources[i];

        src->active = NULL;
        if (strcmp(src->classname, "0") != 0) {
            for (int j = 0; j < g_reg.count; j++) {
                if (strcmp(g_reg.classes[j]->name, src->classname) == 0) {
                    src->active = g_reg.classes[j];
                    break;
                }
            }
            if (!src->active) {
                fprintf(stderr, "[core:input] class not found: %s\n", src->classname);
                return false;
            }
        }

        if (!src->driver->open(src->port, src->device[0] ? src->device : NULL)) {
            fprintf(stderr, "[core:input] driver open failed: %s\n", src->proto);
            return false;
        }
    }

    if (!any_debug) {
        for (int i = 0; i < g_reg.count; i++) {
            int used = 0;
            for (int j = 0; j < g_reg.source_count; j++) {
                if (g_reg.classes[i] == g_reg.sources[j].active) { used = 1; break; }
            }
            if (!used) {
                free(g_reg.classes[i]->entries);
                free(g_reg.classes[i]);
                g_reg.classes[i] = NULL;
            }
        }
        int w = 0;
        for (int i = 0; i < g_reg.count; i++)
            if (g_reg.classes[i]) g_reg.classes[w++] = g_reg.classes[i];
        g_reg.count = w;
    }

    return true;
}

void gamely_daemon_input_close(void)
{
    for (int i = 0; i < g_reg.source_count; i++) {
        if (g_reg.sources[i].driver)
            g_reg.sources[i].driver->close(g_reg.sources[i].port);
    }

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
    if (!cb) return;
    for (int s = 0; s < g_reg.source_count; s++) {
        gamely_keymap_t *km = g_reg.sources[s].active;
        if (!km) continue;
        for (int i = 0; i < km->count; i++)
            cb(km->entries[i].name, false, g_reg.sources[s].port, usr);
    }
}
