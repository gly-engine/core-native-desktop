#include <stdlib.h>
#include <stdio.h>

#define GECND_STREAM_AVLIB_INTERNAL
#define GECND_FFMPEG_DYN_INTERNAL
#include "gehook.h"
#include "gemedia.h"

static VideoStream *g_background_video = NULL;

void native_media_source(uint8_t channel, const char* url) {
    if (channel != 0) return;

    if (!av_load_ffmpeg()) {
        fprintf(stderr, "[ffmpeg] Failed to load ffmpeg libraries\n");
        return;
    }

    if (g_background_video) {
        stream_destroy(g_background_video);
        g_background_video = NULL;
    }

    g_background_video = stream_create(url);
    if (!g_background_video) {
        fprintf(stderr, "[ffmpeg] Failed to create video stream for %s\n", url);
    }
}

void native_media_position(uint8_t channel, int16_t x, int16_t y, int16_t w, int16_t h) {
    (void)channel; (void)x; (void)y; (void)w; (void)h;
}

void native_media_play(uint8_t channel) {
    (void)channel;
}

void native_media_pause(uint8_t channel) {
    (void)channel;
}

void native_media_stop(uint8_t channel) {
    if (channel != 0) return;
    if (g_background_video) {
        stream_destroy(g_background_video);
        g_background_video = NULL;
    }
}

MediaFrame* avlib_get_background_frame(void) {
    if (g_background_video) {
        return stream_get_frame(g_background_video);
    }
    return NULL;
}

