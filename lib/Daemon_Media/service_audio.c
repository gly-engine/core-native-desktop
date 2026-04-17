#include <stdint.h>
#include <stddef.h>
#include "gamely_media.h"

static gamely_audio_cb_t g_cb       = NULL;
static void             *g_usr      = NULL;
static unsigned          g_rate     = 44100;
static unsigned          g_channels = 2;

void gamely_daemon_media_audio_subscribe(gamely_audio_cb_t cb, void *usr) {
    g_cb  = cb;
    g_usr = usr;
}

void gamely_daemon_media_audio_configure(unsigned rate, unsigned channels) {
    g_rate     = rate;
    g_channels = channels;
}

void gamely_daemon_media_audio_push(const int16_t *data, size_t frames) {
    if (g_cb) g_cb(data, frames, g_rate, g_channels, g_usr);
}
