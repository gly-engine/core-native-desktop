#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "gamely_media.h"

bool encode_init    (int w, int h, int fps, void (*on_ts)(const uint8_t *, int, int64_t));
void encode_push    (const uint8_t *rgba, int w, int h);
void encode_shutdown(void);

static gamely_transmit_cb_t g_cb     = NULL;
static atomic_bool          g_online = false;
static int                  g_enc_w  = 0;
static int                  g_enc_h  = 0;

static void on_ts_ready(const uint8_t *buf, int size, int64_t pts) {
    printf("[transmit] on_ts_ready size=%d pts=%lld\n", size, (long long)pts);
    gamely_transmit_cb_t cb = g_cb;
    if (cb) cb(buf, size, pts);
}

void gamely_daemon_media_transmit_callback(gamely_transmit_cb_t cb) {
    printf("[transmit] callback: g_online addr=%p cb=%p\n",
           (void*)&g_online, (void*)cb);
    g_cb = cb;
    atomic_store(&g_online, cb != NULL);
    if (!cb) {
        encode_shutdown();
        g_enc_w = 0;
        g_enc_h = 0;
    }
}

void gamely_daemon_media_transmit_shutdown(void) {
    gamely_daemon_media_transmit_callback(NULL);
}

bool gamely_daemon_media_transmit_is_online(void) {
    return atomic_load(&g_online);
}

void gamely_daemon_media_transmit_push(const uint8_t *rgba, int width, int height) {
    if (!gamely_daemon_media_transmit_is_online()) return;
    printf("[transmit] push %dx%d enc_w=%d enc_h=%d\n", width, height, g_enc_w, g_enc_h);
    if (width != g_enc_w || height != g_enc_h) {
        encode_shutdown();
        if (!encode_init(width, height, 30, on_ts_ready)) {
            fprintf(stderr, "[transmit] encode_init falhou para %dx%d\n", width, height);
            return;  /* g_enc_w/h ficam em 0 — retry no próximo frame */
        }
        g_enc_w = width;
        g_enc_h = height;
    }
    encode_push(rgba, width, height);
}
