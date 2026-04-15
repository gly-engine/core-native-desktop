#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include "gamely_media.h"

#define MAX_CLIENTS  8
#define RING_SIZE    (1 << 19)
#define RING_MASK    (RING_SIZE - 1)

bool encode_init    (int w, int h, int fps, void (*on_ts)(const uint8_t *, int));
void encode_push    (const uint8_t *rgba, int w, int h);
void encode_shutdown(void);

typedef struct {
    uint8_t        *buf;
    unsigned int    write_pos;
    unsigned int    read_pos;
    pthread_mutex_t lock;
    atomic_bool     active;
} StreamClient;

static StreamClient g_clients[MAX_CLIENTS];
static atomic_int   g_count  = 0;
static void       (*g_notify)(void) = NULL;

static int  g_enc_w = 0;
static int  g_enc_h = 0;

static void ring_write(StreamClient *c, const uint8_t *data, int size) {
    unsigned int space = RING_SIZE - (c->write_pos - c->read_pos);
    if ((unsigned int)size > space) {
        c->read_pos = c->write_pos - RING_SIZE + (unsigned int)size;
    }
    unsigned int off    = c->write_pos & RING_MASK;
    unsigned int part1  = RING_SIZE - off;
    if ((unsigned int)size <= part1) {
        memcpy(c->buf + off, data, (size_t)size);
    } else {
        memcpy(c->buf + off, data, part1);
        memcpy(c->buf, data + part1, (size_t)(size - (int)part1));
    }
    c->write_pos += (unsigned int)size;
}

static int ring_read(StreamClient *c, uint8_t *dst, int max) {
    unsigned int avail = c->write_pos - c->read_pos;
    unsigned int n     = avail < (unsigned int)max ? avail : (unsigned int)max;
    if (n == 0) return 0;
    unsigned int off   = c->read_pos & RING_MASK;
    unsigned int part1 = RING_SIZE - off;
    if (n <= part1) {
        memcpy(dst, c->buf + off, n);
    } else {
        memcpy(dst, c->buf + off, part1);
        memcpy(dst + part1, c->buf, n - part1);
    }
    c->read_pos += n;
    return (int)n;
}

static void on_ts_ready(const uint8_t *data, int size) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        StreamClient *c = &g_clients[i];
        if (!atomic_load(&c->active)) continue;
        pthread_mutex_lock(&c->lock);
        ring_write(c, data, size);
        pthread_mutex_unlock(&c->lock);
    }
    if (g_notify) g_notify();
}

bool gamely_daemon_media_transmit_active(void) {
    return atomic_load(&g_count) > 0;
}

void gamely_daemon_media_transmit_push(const uint8_t *rgba, int width, int height) {
    if (!gamely_daemon_media_transmit_active()) return;
    if (width != g_enc_w || height != g_enc_h) {
        encode_shutdown();
        g_enc_w = width;
        g_enc_h = height;
        encode_init(width, height, 30, on_ts_ready);
    }
    encode_push(rgba, width, height);
}

int gamely_daemon_media_transmit_client_add(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        StreamClient *c = &g_clients[i];
        if (atomic_load(&c->active)) continue;
        if (!c->buf) {
            c->buf = malloc(RING_SIZE);
            pthread_mutex_init(&c->lock, NULL);
        }
        c->write_pos = 0;
        c->read_pos  = 0;
        atomic_store(&c->active, true);
        atomic_fetch_add(&g_count, 1);
        return i;
    }
    return -1;
}

void gamely_daemon_media_transmit_client_remove(int id) {
    if (id < 0 || id >= MAX_CLIENTS) return;
    if (atomic_exchange(&g_clients[id].active, false)) {
        int remaining = atomic_fetch_sub(&g_count, 1) - 1;
        if (remaining <= 0) {
            encode_shutdown();
            g_enc_w = 0;
            g_enc_h = 0;
        }
    }
}

int gamely_daemon_media_transmit_client_read(int id, uint8_t *buf, int max) {
    if (id < 0 || id >= MAX_CLIENTS) return 0;
    StreamClient *c = &g_clients[id];
    if (!atomic_load(&c->active)) return 0;
    pthread_mutex_lock(&c->lock);
    int n = ring_read(c, buf, max);
    pthread_mutex_unlock(&c->lock);
    return n;
}

void gamely_daemon_media_transmit_set_notify(void (*fn)(void)) {
    g_notify = fn;
}
