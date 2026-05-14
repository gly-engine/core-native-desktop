/**
 * @startuml
 * participant "tick()" as T
 * participant "TTL array" as TTL
 * participant "queue" as Q
 * participant "subscribers" as S
 *
 * T -> TTL : scan entries
 * TTL --> T : expired {name, port}
 * T -> T   : key_state_set(port, name, false)
 * T -> S   : cb(name, false, port, usr)
 * T -> Q   : drain
 * Q --> T  : {name, pressed, port}
 * T -> T   : key_state_set(port, name, pressed)
 * T -> S   : cb(name, pressed, port, usr)
 * @enduml
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

#include "gecnd.h"

/* forward decls from service_keymap.c */
typedef struct { uint32_t code; const char *name; } gamely_keymap_entry_t;
typedef struct {
    gamely_keymap_entry_t *entries;
    int   count;
    int   capacity;
    char  name[32];
    int   debug;
} gamely_keymap_t;

const char      *gamely_keymap_lookup(gamely_keymap_t *km, uint32_t code);
const char      *gamely_keymap_lookup_debug(uint32_t code, const char **out_class);
gamely_keymap_t *gamely_keymap_get_active(void);
int              gamely_keymap_get_debug(void);
int              gamely_keymap_get_port(void);
bool             gamely_daemon_input_do_init(void);

/* -- time -- */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* -- queue -- */

#define QUEUE_SIZE 128

typedef struct { char name[8]; bool pressed; int port; } gamely_io_event_t;

static gamely_io_event_t  g_queue[QUEUE_SIZE];
static atomic_int         g_head = 0;
static atomic_int         g_tail = 0;
static pthread_mutex_t    g_enq_mutex = PTHREAD_MUTEX_INITIALIZER;

static void enqueue(const char *name, bool pressed, int port)
{
    pthread_mutex_lock(&g_enq_mutex);
    int cur  = atomic_load_explicit(&g_head, memory_order_relaxed);
    int next = (cur + 1) % QUEUE_SIZE;
    if (next != atomic_load_explicit(&g_tail, memory_order_acquire)) {
        strncpy(g_queue[cur].name, name, 7);
        g_queue[cur].name[7] = '\0';
        g_queue[cur].pressed = pressed;
        g_queue[cur].port    = port;
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

typedef struct { char name[8]; int port; uint64_t expiry_ms; } gamely_ttl_entry_t;

static gamely_ttl_entry_t *g_ttl     = NULL;
static int                 g_ttl_cnt = 0;
static int                 g_ttl_cap = 0;

static void ttl_upsert(const char *name, int port, uint64_t expiry_ms)
{
    for (int i = 0; i < g_ttl_cnt; i++) {
        if (g_ttl[i].port == port && strcmp(g_ttl[i].name, name) == 0) {
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
    g_ttl[g_ttl_cnt].expiry_ms = expiry_ms;
    g_ttl_cnt++;
}

static void ttl_remove(const char *name, int port)
{
    for (int i = 0; i < g_ttl_cnt; i++) {
        if (g_ttl[i].port == port && strcmp(g_ttl[i].name, name) == 0) {
            g_ttl[i] = g_ttl[--g_ttl_cnt];
            return;
        }
    }
}

/* -- pollers -- */

#define MAX_POLLERS 8

static void (*g_pollers[MAX_POLLERS])(void);
static int   g_poller_cnt = 0;

void gamely_daemon_input_add_tick(void (*fn)(void)) {
    if (fn && g_poller_cnt < MAX_POLLERS)
        g_pollers[g_poller_cnt++] = fn;
}

/* -- subscribers -- */

#define MAX_SUBS 8

typedef struct { gamely_input_key_cb cb; void *usr; } gamely_sub_t;

static gamely_sub_t g_subs[MAX_SUBS];
static int          g_sub_cnt = 0;

static void fire(const char *name, bool pressed, int port)
{
    for (int i = 0; i < g_sub_cnt; i++)
        g_subs[i].cb(name, pressed, port, g_subs[i].usr);
}

/* -- public API -- */

void gamely_daemon_input_push(uint32_t code, bool pressed, uint32_t ttl_ms)
{
    if (!gamely_daemon_input_do_init()) return;

    gamely_keymap_t *km   = gamely_keymap_get_active();
    const char      *name = gamely_keymap_lookup(km, code);
    int              port = gamely_keymap_get_port();

    if (gamely_keymap_get_debug()) {
        const char *dbg_class = NULL;
        const char *dbg_name  = gamely_keymap_lookup_debug(code, &dbg_class);
        fprintf(stderr, "[core:debug:input] hex= 0x%08X class= %s key= %s press= %d\n",
                code,
                dbg_class ? dbg_class : "?",
                dbg_name  ? dbg_name  : "?",
                pressed);
    }

    if (!name) return;

    if (ttl_ms > 0 && pressed)
        ttl_upsert(name, port, now_ms() + ttl_ms);
    else if (!pressed)
        ttl_remove(name, port);

    enqueue(name, pressed, port);
}

void gamely_daemon_input_push_name(const char *name, bool pressed, int port, uint32_t ttl_ms)
{
    if (!name) return;
    if (!gamely_daemon_input_do_init()) return;

    if (ttl_ms > 0 && pressed)
        ttl_upsert(name, port, now_ms() + ttl_ms);
    else if (!pressed)
        ttl_remove(name, port);

    enqueue(name, pressed, port);
}

void gamely_daemon_input_subscribe(gamely_input_key_cb cb, void *usr)
{
    if (!cb || g_sub_cnt >= MAX_SUBS) return;
    g_subs[g_sub_cnt++] = (gamely_sub_t){cb, usr};
}

void gamely_daemon_input_tick(void)
{
    for (int i = 0; i < g_poller_cnt; i++)
        g_pollers[i]();

    uint64_t now = now_ms();

    /* TTL expiry */
    for (int i = g_ttl_cnt - 1; i >= 0; i--) {
        if (now >= g_ttl[i].expiry_ms) {
            const char *name = g_ttl[i].name;
            int         port = g_ttl[i].port;
            g_ttl[i] = g_ttl[--g_ttl_cnt];
            key_state_set(port, name, false);
            fire(name, false, port);
        }
    }

    /* drain queue */
    int tail = atomic_load_explicit(&g_tail, memory_order_relaxed);
    while (tail != atomic_load_explicit(&g_head, memory_order_acquire)) {
        gamely_io_event_t ev = g_queue[tail];
        tail = (tail + 1) % QUEUE_SIZE;
        atomic_store_explicit(&g_tail, tail, memory_order_release);
        key_state_set(ev.port, ev.name, ev.pressed);
        fire(ev.name, ev.pressed, ev.port);
    }
}

void gamely_daemon_input_reset_port(int port)
{
    if (port < 0 || port >= 4) return;
    for (int i = 0; i < g_state_cnt; i++) {
        if (g_states[i].pressed[port]) {
            g_states[i].pressed[port] = false;
            fire(g_states[i].name, false, port);
        }
    }
    /* remove TTL entries for this port */
    for (int i = g_ttl_cnt - 1; i >= 0; i--) {
        if (g_ttl[i].port == port)
            g_ttl[i] = g_ttl[--g_ttl_cnt];
    }
}
