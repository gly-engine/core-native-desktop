/**
 * @startuml
 * participant "set_toml" as T
 * participant "service_keymap" as KM
 * participant "driver" as D
 *
 * T  -> KM : add_keycode("vivensis.dtv30", "up", 0xF3)
 * T  -> KM : add_keycode("vivensis.dtv30", "a",  0xF1)
 * ...add_source()...
 * KM -> KM : parse URI — proto, classname, device, port, debug
 * KM -> KM : store source slot
 * ...open()...
 * KM -> KM : validate proto + classname per source; mark in_use
 * KM -> D  : open(port, device) per source
 * ...cleanup()...
 * KM -> KM : free classes not marked in_use
 * @enduml
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "gecnd.h"

#ifndef GLFW_DEFAULT_INPUT_CLASS
#define GLFW_DEFAULT_INPUT_CLASS "void://0"
#endif

/* -- keymap (bidirectional map code ↔ name) -- */

typedef struct { uint32_t code; char name[8]; } km_by_code_t; /* sorted by code */
typedef struct { char name[8]; uint32_t code; } km_by_name_t; /* sorted by name */

typedef struct gamely_keymap_t {
    km_by_code_t *by_code;
    km_by_name_t *by_name;
    int           count;
    int           capacity;
    char          name[32];  /* class name */
    bool          in_use;    /* set by do_init; used by cleanup */
    int           debug;
} gamely_keymap_t;

/* -- source -- */

#define MAX_SOURCES 8

typedef struct {
    char                         proto[16];
    char                         classname[32];
    char                        *searchparams;
    int                          port;
    int                          debug;
    const gamely_input_driver_t *driver;
    gamely_keymap_t             *active;
} gamely_input_source_t;

/* -- registry -- */

#define MAX_CLASSES 64

typedef struct {
    gamely_keymap_t      *classes[MAX_CLASSES];
    int                   count;
    gamely_keymap_t      *current;
    gamely_input_source_t sources[MAX_SOURCES];
    int                   source_count;
} gamely_keymap_registry_t;

static gamely_keymap_registry_t g_reg;
static int                      g_initialized = 0;

bool gamely_daemon_input_do_init(void);

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

void gamely_keymap_mark_in_use(const char *name)
{
    if (!name) return;
    for (int i = 0; i < g_reg.count; i++) {
        if (g_reg.classes[i] && strcmp(g_reg.classes[i]->name, name) == 0) {
            g_reg.classes[i]->in_use = true;
            return;
        }
    }
}

gamely_keymap_t *gamely_keymap_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < g_reg.count; i++) {
        if (g_reg.classes[i] && strcmp(g_reg.classes[i]->name, name) == 0)
            return g_reg.classes[i];
    }
    return NULL;
}

const char *gamely_keymap_get_active_name(void)
{
    if (g_reg.source_count && g_reg.sources[0].active)
        return g_reg.sources[0].active->name;
    return NULL;
}

bool gamely_keymap_translate(gamely_keymap_t *km, const char *name, uint32_t *out)
{
    if (!km || !name || km->count == 0) return false;
    int lo = 0, hi = km->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(km->by_name[mid].name, name);
        if      (cmp == 0) { *out = km->by_name[mid].code; return true; }
        else if (cmp <  0) lo = mid + 1;
        else               hi = mid - 1;
    }
    return false;
}

static void set_current_class(const char *name)
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

void gamely_daemon_input_add_keycode(const char *class_name, const char *key_name, uint32_t hex)
{
    set_current_class(class_name);
    if (!key_name || !g_reg.current) return;
    if (strlen(key_name) >= 8) {
        fprintf(stderr, "[core:input] key name too long (max 7): %s\n", key_name);
        return;
    }

    gamely_keymap_t *km = g_reg.current;
    if (km->count == km->capacity) {
        int nc = km->capacity ? km->capacity * 2 : 8;
        km_by_code_t *nc_arr = realloc(km->by_code, (size_t)nc * sizeof(*km->by_code));
        if (!nc_arr) return;
        km->by_code = nc_arr;
        km_by_name_t *nn_arr = realloc(km->by_name, (size_t)nc * sizeof(*km->by_name));
        if (!nn_arr) return;
        km->by_name = nn_arr;
        km->capacity = nc;
    }

    /* inserção ordenada em by_code (por code) */
    int pc = km->count;
    while (pc > 0 && km->by_code[pc - 1].code > hex) {
        km->by_code[pc] = km->by_code[pc - 1];
        pc--;
    }
    km->by_code[pc].code = hex;
    strncpy(km->by_code[pc].name, key_name, 7);
    km->by_code[pc].name[7] = '\0';

    /* inserção ordenada em by_name (por name) */
    int pn = km->count;
    while (pn > 0 && strcmp(km->by_name[pn - 1].name, key_name) > 0) {
        km->by_name[pn] = km->by_name[pn - 1];
        pn--;
    }
    strncpy(km->by_name[pn].name, key_name, 7);
    km->by_name[pn].name[7] = '\0';
    km->by_name[pn].code    = hex;

    km->count++;
}

/* -- lookup -- */

const char *gamely_keymap_lookup(gamely_keymap_t *km, uint32_t code)
{
    if (!km || km->count == 0) return NULL;
    int lo = 0, hi = km->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if      (km->by_code[mid].code == code) return km->by_code[mid].name;
        else if (km->by_code[mid].code <  code) lo = mid + 1;
        else                                    hi = mid - 1;
    }
    return NULL;
}

uint32_t gamely_keymap_lookup_name(gamely_keymap_t *km, const char *name)
{
    if (!km || km->count == 0) return 0;
    int lo = 0, hi = km->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(km->by_name[mid].name, name);
        if      (cmp == 0) return km->by_name[mid].code;
        else if (cmp <  0) lo = mid + 1;
        else               hi = mid - 1;
    }
    return 0;
}

/* -- per-source accessors used by service_io -- */

int         gamely_keymap_source_count(void)              { return g_reg.source_count; }
int         gamely_keymap_source_port(int s)              { return (s >= 0 && s < g_reg.source_count) ? g_reg.sources[s].port  : 0; }
int         gamely_keymap_source_debug(int s)             { return (s >= 0 && s < g_reg.source_count) ? g_reg.sources[s].debug : 0; }
const char *gamely_keymap_source_class_name(int s)        { return (s >= 0 && s < g_reg.source_count && g_reg.sources[s].active) ? g_reg.sources[s].active->name : NULL; }
int         gamely_keymap_source_key_count(int s)         { return (s >= 0 && s < g_reg.source_count && g_reg.sources[s].active) ? g_reg.sources[s].active->count : 0; }
const char *gamely_keymap_source_key_name(int s, int idx) { return (s >= 0 && s < g_reg.source_count && g_reg.sources[s].active && idx >= 0 && idx < g_reg.sources[s].active->count) ? g_reg.sources[s].active->by_code[idx].name : NULL; }

const char *gamely_keymap_lookup_source(int s, uint32_t code)
{
    if (s < 0 || s >= g_reg.source_count) return NULL;
    return gamely_keymap_lookup(g_reg.sources[s].active, code);
}

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
        src->searchparams = strdup(qmark + 1);
        const char *p = qmark + 1;
        while (p && *p) {
            const char *amp = strchr(p, '&');
            if (strncmp(p, "debug=", 6) == 0) {
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

bool gamely_daemon_input_do_init(void)
{
    if (g_initialized) return true;

    if (g_reg.source_count == 0)
        gamely_daemon_input_add_source(GLFW_DEFAULT_INPUT_CLASS);

    for (int i = 0; i < g_reg.source_count; i++) {
        gamely_input_source_t *src = &g_reg.sources[i];

        src->active = NULL;
        if (strcmp(src->classname, "0") != 0) {
            for (int j = 0; j < g_reg.count; j++) {
                if (strcmp(g_reg.classes[j]->name, src->classname) == 0) {
                    src->active           = g_reg.classes[j];
                    src->active->in_use   = true;
                    break;
                }
            }
            if (!src->active) {
                fprintf(stderr, "[core:input] class not found: %s\n", src->classname);
                return false;
            }
        }

        if (!src->driver->open(src->port, src->searchparams)) {
            fprintf(stderr, "[core:input] driver open failed: %s\n", src->proto);
            free(src->searchparams);
            src->searchparams = NULL;
            return false;
        }
        free(src->searchparams);
        src->searchparams = NULL;
    }

    g_initialized = 1;
    return true;
}

void gamely_daemon_input_cleanup(void)
{
    for (int i = 0; i < g_reg.source_count; i++) {
        if (g_reg.sources[i].debug) return;
    }

    int w = 0;
    for (int i = 0; i < g_reg.count; i++) {
        gamely_keymap_t *km = g_reg.classes[i];
        if (!km) continue;
        if (km->in_use) {
            g_reg.classes[w++] = km;
        } else {
            free(km->by_code);
            free(km->by_name);
            free(km);
        }
    }
    g_reg.count = w;
}

void gamely_daemon_input_close(void)
{
    for (int i = 0; i < g_reg.source_count; i++) {
        if (g_reg.sources[i].driver)
            g_reg.sources[i].driver->close(g_reg.sources[i].port);
        free(g_reg.sources[i].searchparams);
        g_reg.sources[i].searchparams = NULL;
    }

    for (int i = 0; i < g_reg.count; i++) {
        if (g_reg.classes[i]) {
            free(g_reg.classes[i]->by_code);
            free(g_reg.classes[i]->by_name);
            free(g_reg.classes[i]);
        }
    }
    memset(&g_reg, 0, sizeof(g_reg));
    g_initialized = 0;
}

__attribute__((constructor))
static void register_keymap_functions(void) {
    gecnd_registry("set", "function:gamely_daemon_input_add_keycode", (void *)gamely_daemon_input_add_keycode, NULL);
}

