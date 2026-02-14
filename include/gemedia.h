#ifndef GEMEDIA_H
#define GEMEDIA_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
    int linesize;
    double pts;
    atomic_bool ready;
} MediaFrame;

MediaFrame* avlib_get_background_frame(void);

#if defined(GECND_STREAM_AVLIB_INTERNAL)

#include <uv.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext  *vcodec;
    AVStream        *video;
    int video_index;
    struct SwsContext *sws;

    MediaFrame frames[2];
    atomic_int front;

    uv_thread_t thread;
    atomic_int running;
    double clock_start;
    char *url;
} VideoStream;

VideoStream* stream_create(const char *url);
void stream_destroy(VideoStream *stream);
MediaFrame* stream_get_frame(VideoStream *stream);

#endif // GECND_STREAM_AVLIB_INTERNAL

#endif // GEMEDIA_H
