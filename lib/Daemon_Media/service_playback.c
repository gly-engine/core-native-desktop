#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gecnd.h"
#include "gamely_media.h"
#include "gefilter.h"

#define GECND_STREAM_AVLIB_INTERNAL
#include "gemedia.h"

#define MAX_CHANNELS 4

static VideoStream *g_streams[MAX_CHANNELS] = {0};

bool av_load_ffmpeg(void);

void gamely_daemon_media_playback_source(uint8_t channel, const char *url) {
    if (channel >= MAX_CHANNELS) return;

    if (!av_load_ffmpeg()) {
        fprintf(stderr, "[media] failed to load ffmpeg\n");
        return;
    }

    if (g_streams[channel]) {
        stream_destroy(g_streams[channel]);
        g_streams[channel] = NULL;
        if (channel == 0) gamely_daemon_media_background_release();
    }

    if (!gamely_daemon_media_background_claim()) {
        fprintf(stderr, "[media] background buffer in use\n");
        return;
    }

    g_streams[channel] = stream_create(url);
    if (!g_streams[channel]) {
        fprintf(stderr, "[media] failed to create stream: %s\n", url);
        gamely_daemon_media_background_release();
    }
}

void gamely_daemon_media_playback_play(uint8_t channel) {
    if (channel >= MAX_CHANNELS || !g_streams[channel]) return;
    atomic_store(&g_streams[channel]->paused, 0);
}

void gamely_daemon_media_playback_pause(uint8_t channel) {
    if (channel >= MAX_CHANNELS || !g_streams[channel]) return;
    atomic_store(&g_streams[channel]->paused, 1);
}

void gamely_daemon_media_playback_stop(uint8_t channel) {
    if (channel >= MAX_CHANNELS || !g_streams[channel]) return;
    stream_destroy(g_streams[channel]);
    g_streams[channel] = NULL;
    if (channel == 0) gamely_daemon_media_background_release();
}

void gamely_daemon_media_playback_position(uint8_t channel,
                                            int16_t x, int16_t y,
                                            int16_t w, int16_t h) {
    (void)channel;
    gecnd_filter_set_video_pos(x, y, w, h);
}
