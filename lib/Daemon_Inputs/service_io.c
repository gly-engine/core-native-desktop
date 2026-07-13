/**
 * @startuml
 * participant "tick()" as T
 * participant "TTL array" as TTL
 * participant "queue" as Q
 * participant "subscribers" as S
 *
 * T -> T   : run @tick pollers
 * T -> S   : @init on first successful init (once)
 * T -> TTL : scan entries
 * TTL --> T : expired {name, port, src}
 * T -> T   : key_state_set(port, name, false)
 * T -> S   : fire(name, false, port, src)
 * T -> Q   : drain
 * Q --> T  : {name, pressed, port, src}
 * T -> T   : key_state_set(port, name, pressed)
 * T -> S   : fire(name, pressed, port, src)
 * @enduml
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

#include "gecnd.h"

/* forward decl opaco — campos definidos em service_keymap.c */
typedef struct gamely_keymap_t gamely_keymap_t;

int         gamely_keymap_source_count(void);
int         gamely_keymap_source_port(int s);
int         gamely_keymap_source_debug(int s);
const char *gamely_keymap_source_class_name(int s);
int         gamely_keymap_source_key_count(int s);
const char *gamely_keymap_source_key_name(int s, int idx);
const char *gamely_keymap_lookup_source(int s, uint32_t code);
const char *gamely_keymap_lookup_debug(uint32_t code, const char **out_class);
gamely_keymap_t *gamely_keymap_find(const char *name);
bool        gamely_keymap_translate(gamely_keymap_t *km, const char *name, uint32_t *out);
void        gamely_keymap_mark_in_use(const char *name);
bool        gamely_daemon_input_do_init(void);
void        gamely_daemon_input_add_source(const char *uri);

/* -- time -- */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* -- queue -- */

#define QUEUE_SIZE 128

/* src == -1 means injected via push_name (no source class) */
typedef struct { char name[8]; bool pressed; int port; int8_t src; } gamely_io_event_t;

static gamely_io_event_t  g_queue[QUEUE_SIZE];
static atomic_int         g_head = 0;
static atomic_int         g_tail = 0;
static pthread_mutex_t    g_enq_mutex = PTHREAD_MUTEX_INITIALIZER;

static void enqueue(const char *name, bool pressed, int port, int src)
{
    pthread_mutex_lock(&g_enq_mutex);
    int cur  = atomic_load_explicit(&g_head, memory_order_relaxed);
    int next = (cur + 1) % QUEUE_SIZE;
    if (next != atomic_load_explicit(&g_tail, memory_order_acquire)) {
        strncpy(g_queue[cur].name, name, 7);
        g_queue[cur].name[7] = '\0';
        g_queue[cur].pressed = pressed;
        g_queue[cur].port    = port;
        g_queue[cur].src     = (int8_t)src;
        atomic_store_explicit(&g_head, next, memory_order_release);
    }
    pthread_mutex_unlock(&g_enq_mutex);
}

/* -- key state -- */

typedef struct { char name[8]; bool pressed[4]; } gamely_key_state_t;

static gamely_key_state_t *g_states    = NULL;
static int                 g_state_cnt = 0;
static int                 g_state_cap = 0;

static void key_state_set(int port, const char *name, bool pressed)
{
    if (port < 0 || port >= 4 || !name) return;
    for (int i = 0; i < g_state_cnt; i++) {
        if (strcmp(g_states[i].name, name) == 0) {
            g_states[i].pressed[port] = pressed;
            return;
        }
    }
    if (g_state_cnt == g_state_cap) {
        int nc = g_state_cap ? g_state_cap * 2 : 8;
        gamely_key_state_t *ns = realloc(g_states, (size_t)nc * sizeof(*g_states));
        if (!ns) return;
        g_states    = ns;
        g_state_cap = nc;
    }
    strncpy(g_states[g_state_cnt].name, name, 7);
    g_states[g_state_cnt].name[7] = '\0';
    memset(g_states[g_state_cnt].pressed, 0, sizeof(g_states[g_state_cnt].pressed));
    g_states[g_state_cnt].pressed[port] = pressed;
    g_state_cnt++;
}

/* -- TTL -- */

typedef struct { char name[8]; int port; int8_t src; uint64_t expiry_ms; } gamely_ttl_entry_t;

static gamely_ttl_entry_t *g_ttl     = NULL;
static int                 g_ttl_cnt = 0;
static int                 g_ttl_cap = 0;

static void ttl_upsert(const char *name, int port, int src, uint64_t expiry_ms)
{
    for (int i = 0; i < g_ttl_cnt; i++) {
        if (g_ttl[i].port == port && g_ttl[i].src == src && strcmp(g_ttl[i].name, name) == 0) {
            g_ttl[i].expiry_ms = expiry_ms;
            return;
        }
    }
    if (g_ttl_cnt == g_ttl_cap) {
        int nc = g_ttl_cap ? g_ttl_cap * 2 : 8;
        gamely_ttl_entry_t *nt = realloc(g_ttl, (size_t)nc * sizeof(*g_ttl));
        if (!nt) return;
        g_ttl     = nt;
        g_ttl_cap = nc;
    }
    strncpy(g_ttl[g_ttl_cnt].name, name, 7);
    g_ttl[g_ttl_cnt].name[7] = '\0';
    g_ttl[g_ttl_cnt].port = port;
    g_ttl[g_ttl_cnt].src  = (int8_t)src;
    g_ttl[g_ttl_cnt].expiry_ms = expiry_ms;
    g_ttl_cnt++;
}

static void ttl_remove(const char *name, int port, int src)
{
    for (int i = 0; i < g_ttl_cnt; i++) {
        if (g_ttl[i].port == port && g_ttl[i].src == src && strcmp(g_ttl[i].name, name) == 0) {
            g_ttl[i] = g_ttl[--g_ttl_cnt];
            return;
        }
    }
}

/* -- unified callbacks -- */

#define MAX_CBS 24

typedef enum { CB_TICK, CB_CODE, CB_INIT, CB_CLASS, CB_TRANSLATE } cb_kind_t;

typedef struct {
    cb_kind_t        kind;
    char             tag[32];   /* CB_CLASS: class name; CB_TRANSLATE: from class (empty=wildcard) */
    gamely_keymap_t *to_km;     /* CB_TRANSLATE: target keymap for name→code */
    void            *fn;
    void            *usr;       /* CB_CODE and CB_INIT only */
} gamely_cb_t;

static gamely_cb_t g_cbs[MAX_CBS];
static int         g_cb_cnt    = 0;
static bool        g_init_done = false;

void gamely_input_add_url(const char *url)
{
    gamely_daemon_input_add_source(url);
}

bool gamely_input_add_cb(const char *tag, void *fn, void *usr)
{
    if (!tag || !fn || g_cb_cnt >= MAX_CBS) return false;

    const char *colon = strchr(tag, ':');

    if (!colon) {
        cb_kind_t kind;
        if      (strcmp(tag, "@tick") == 0) kind = CB_TICK;
        else if (strcmp(tag, "@code") == 0) kind = CB_CODE;
        else if (strcmp(tag, "@init") == 0) kind = CB_INIT;
        else {
            kind = CB_CLASS;
            gamely_keymap_mark_in_use(tag);
        }
        gamely_cb_t cb = { .kind = kind, .fn = fn, .usr = usr };
        if (kind == CB_CLASS) {
            strncpy(cb.tag, tag, 31);
            cb.tag[31] = '\0';
        }
        g_cbs[g_cb_cnt++] = cb;
        return true;
    }

    /* "from:to" or ":to" */
    size_t from_len = (size_t)(colon - tag);
    const char *to_name = colon + 1;

    gamely_cb_t cb = { .kind = CB_TRANSLATE, .fn = fn };

    if (from_len > 0) {
        if (from_len >= sizeof(cb.tag)) return false;
        memcpy(cb.tag, tag, from_len);
        cb.tag[from_len] = '\0';
        if (!gamely_keymap_find(cb.tag)) return false;
        gamely_keymap_mark_in_use(cb.tag);
    }

    cb.to_km = gamely_keymap_find(to_name);
    if (!cb.to_km) return false;
    gamely_keymap_mark_in_use(to_name);

    g_cbs[g_cb_cnt++] = cb;
    return true;
}

static void fire(const char *name, bool pressed, int port, int src)
{
    for (int i = 0; i < g_cb_cnt; i++) {
        switch (g_cbs[i].kind) {
        case CB_CODE:
            ((gamely_input_key_cb)g_cbs[i].fn)(name, pressed, port, g_cbs[i].usr);
            break;
        case CB_CLASS:
            ((void (*)(const char *, bool, int))g_cbs[i].fn)(name, pressed, port);
            break;
        case CB_TRANSLATE: {
            /* from filter: skip if this event came from a different class */
            if (g_cbs[i].tag[0] && src >= 0) {
                const char *cls = gamely_keymap_source_class_name(src);
                if (!cls || strcmp(cls, g_cbs[i].tag) != 0) break;
            }
            uint32_t code;
            if (!gamely_keymap_translate(g_cbs[i].to_km, name, &code)) break;
            ((void (*)(uint32_t, bool, int))g_cbs[i].fn)(code, pressed, port);
            break;
        }
        default: break;
        }
    }
}

/* -- public API -- */

void gamely_daemon_input_push(uint32_t code, bool pressed, uint32_t ttl_ms)
{
    if (!gamely_daemon_input_do_init()) return;

    int n = gamely_keymap_source_count();
    for (int s = 0; s < n; s++) {
        /* debug antes do filtro: a graça do ?debug=1 é ver o hex de tecla
         * que AINDA não está mapeada na classe da source */
        if (gamely_keymap_source_debug(s)) {
            const char *dbg_class = NULL;
            const char *dbg_name  = gamely_keymap_lookup_debug(code, &dbg_class);
            fprintf(stderr, "[core:debug:input] src= %d hex= 0x%08X class= %s key= %s press= %d\n",
                    s,
                    code,
                    dbg_class ? dbg_class : "?",
                    dbg_name  ? dbg_name  : "?",
                    pressed);
        }

        const char *name = gamely_keymap_lookup_source(s, code);
        if (!name) continue;

        int port = gamely_keymap_source_port(s);

        if (ttl_ms > 0 && pressed)
            ttl_upsert(name, port, s, now_ms() + ttl_ms);
        else if (!pressed)
            ttl_remove(name, port, s);

        enqueue(name, pressed, port, s);
    }
}

void gamely_daemon_input_push_name(const char *name, bool pressed, int port, uint32_t ttl_ms)
{
    if (!name) return;
    if (!gamely_daemon_input_do_init()) return;

    if (ttl_ms > 0 && pressed)
        ttl_upsert(name, port, -1, now_ms() + ttl_ms);
    else if (!pressed)
        ttl_remove(name, port, -1);

    enqueue(name, pressed, port, -1);
}

void gamely_daemon_input_tick(void)
{
    /* @tick pollers */
    for (int i = 0; i < g_cb_cnt; i++)
        if (g_cbs[i].kind == CB_TICK)
            ((void (*)(void))g_cbs[i].fn)();

    /* @init — fire once after successful init */
    if (!g_init_done && gamely_daemon_input_do_init()) {
        g_init_done = true;
        int n = gamely_keymap_source_count();
        for (int i = 0; i < g_cb_cnt; i++) {
            if (g_cbs[i].kind != CB_INIT) continue;
            gamely_input_key_cb cb = (gamely_input_key_cb)g_cbs[i].fn;
            for (int s = 0; s < n; s++) {
                int port = gamely_keymap_source_port(s);
                int keys = gamely_keymap_source_key_count(s);
                for (int k = 0; k < keys; k++)
                    cb(gamely_keymap_source_key_name(s, k), false, port, g_cbs[i].usr);
            }
        }
    }

    uint64_t now = now_ms();

    /* TTL expiry */
    for (int i = g_ttl_cnt - 1; i >= 0; i--) {
        if (now >= g_ttl[i].expiry_ms) {
            char name[8];
            memcpy(name, g_ttl[i].name, 8);
            int   port = g_ttl[i].port;
            int   src  = g_ttl[i].src;
            g_ttl[i] = g_ttl[--g_ttl_cnt];
            key_state_set(port, name, false);
            fire(name, false, port, src);
        }
    }

    /* drain queue */
    int tail = atomic_load_explicit(&g_tail, memory_order_relaxed);
    while (tail != atomic_load_explicit(&g_head, memory_order_acquire)) {
        gamely_io_event_t ev = g_queue[tail];
        tail = (tail + 1) % QUEUE_SIZE;
        atomic_store_explicit(&g_tail, tail, memory_order_release);
        key_state_set(ev.port, ev.name, ev.pressed);
        fire(ev.name, ev.pressed, ev.port, (int)ev.src);
    }
}

void gamely_daemon_input_reset_port(int port)
{
    if (port < 0 || port >= 4) return;
    for (int i = 0; i < g_state_cnt; i++) {
        if (g_states[i].pressed[port]) {
            g_states[i].pressed[port] = false;
            fire(g_states[i].name, false, port, -1);
        }
    }
    for (int i = g_ttl_cnt - 1; i >= 0; i--) {
        if (g_ttl[i].port == port)
            g_ttl[i] = g_ttl[--g_ttl_cnt];
    }
}

__attribute__((constructor))
static void init(void) {
    gecnd_registry("set", "function:gamely_input_add_cb", (void *)gamely_input_add_cb, NULL);
    gecnd_registry("set", "function:gamely_daemon_input_push", (void *)gamely_daemon_input_push, NULL);
    gecnd_registry("set", "function:gamely_daemon_input_push_name", (void *)gamely_daemon_input_push_name, NULL);
}
