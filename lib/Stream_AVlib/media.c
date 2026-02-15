#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GECND_STREAM_AVLIB_INTERNAL
#define GECND_FFMPEG_LOAD_INTERNAL
#include "gemedia.h"

static inline double now_sec(void) {
    return AV.av_gettime_relative() / 1e6;
}

static void frame_alloc(MediaFrame *f, int w, int h) {
    f->width = w;
    f->height = h;
    f->linesize = w * 4;
    f->pixels = malloc((size_t)f->linesize * h);
    atomic_store(&f->ready, false);
}

static void frame_free(MediaFrame *f) {
    if(f->pixels) {
        free(f->pixels);
        f->pixels = NULL;
    }
}

static void frame_copy(struct SwsContext *sws, AVFrame *src, MediaFrame *dst) {
    uint8_t *dst_data[4] = { dst->pixels, NULL, NULL, NULL };
    int dst_linesize[4] = { dst->linesize, 0, 0, 0 };
    AV.sws_scale(sws, (const uint8_t * const*)src->data, src->linesize, 0, src->height, dst_data, dst_linesize);
}

static void threadworker(void *arg) {
    VideoStream *s = arg;

    AV.avformat_network_init();

    if (AV.avformat_open_input(&s->fmt, s->url, NULL, NULL) < 0) {
        fprintf(stderr, "[ffmpeg] Error opening file: %s\n", s->url);
        atomic_store(&s->running, 0);
        return;
    }
    if (AV.avformat_find_stream_info(s->fmt, NULL) < 0) {
        fprintf(stderr, "[ffmpeg] Error finding stream info for %s\n", s->url);
        goto cleanup_format;
    }

    s->video_index = AV.av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (s->video_index < 0) {
        fprintf(stderr, "[ffmpeg] No video stream found in %s\n", s->url);
        goto cleanup_format;
    }
    
    s->video = s->fmt->streams[s->video_index];
    const AVCodec *vdec = AV.avcodec_find_decoder(s->video->codecpar->codec_id);
    s->vcodec = AV.avcodec_alloc_context3(vdec);
    AV.avcodec_parameters_to_context(s->vcodec, s->video->codecpar);
    if(AV.avcodec_open2(s->vcodec, vdec, NULL) < 0) {
        fprintf(stderr, "[ffmpeg] Failed to open codec for %s\n", s->url);
        goto cleanup_codec;
    }

    s->sws = AV.sws_getContext(
        s->vcodec->width, s->vcodec->height, s->vcodec->pix_fmt,
        s->vcodec->width, s->vcodec->height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, NULL, NULL, NULL
    );

    frame_alloc(&s->frames[0], s->vcodec->width, s->vcodec->height);
    frame_alloc(&s->frames[1], s->vcodec->width, s->vcodec->height);
    atomic_store(&s->front, 0);

    AVPacket *pkt = AV.av_packet_alloc();
    AVFrame  *vfrm = AV.av_frame_alloc();
    
    s->clock_start = now_sec();

    while (atomic_load(&s->running)) {
        if (AV.av_read_frame(s->fmt, pkt) < 0) {
            AV.av_seek_frame(s->fmt, s->video_index, 0, AVSEEK_FLAG_BACKWARD);
            s->clock_start = now_sec();
            continue;
        }

        if (pkt->stream_index == s->video_index) {
            if (AV.avcodec_send_packet(s->vcodec, pkt) == 0) {
                while (AV.avcodec_receive_frame(s->vcodec, vfrm) == 0) {
                    int64_t pts_val = vfrm->pts;
                    if (pts_val == AV_NOPTS_VALUE) {
                        pts_val = vfrm->best_effort_timestamp;
                    }
                    if (pts_val == AV_NOPTS_VALUE) {
                        pts_val = vfrm->pkt_dts;
                    }

                    double pts = (pts_val == AV_NOPTS_VALUE) ? 0 : pts_val * gly_av_q2d(s->video->time_base);

                    double target_time = s->clock_start + pts;
                    double delay = target_time - now_sec();
                    if (delay > 0.001) {
                        usleep((useconds_t)(delay * 1e6));
                    }
                    
                    int back = 1 - atomic_load(&s->front);
                    MediaFrame *f = &s->frames[back];
                    frame_copy(s->sws, vfrm, f);
                    f->pts = pts;
                    atomic_store(&f->ready, true);
                    atomic_store(&s->front, back);
                    AV.av_frame_unref(vfrm);
                }
            }
        }
        AV.av_packet_unref(pkt);
    }

    AV.av_frame_free(&vfrm);
    AV.av_packet_free(&pkt);
    
    frame_free(&s->frames[0]);
    frame_free(&s->frames[1]);

    if (s->sws) AV.sws_freeContext(s->sws);
cleanup_codec:
    if (s->vcodec) AV.avcodec_free_context(&s->vcodec);
cleanup_format:
    if (s->fmt) AV.avformat_close_input(&s->fmt);
    
    AV.avformat_network_deinit();
}

VideoStream* stream_create(const char *url) {
    if (!av_load_ffmpeg()) {
        return NULL;
    }
    
    VideoStream *s = calloc(1, sizeof(VideoStream));
    if (!s) return NULL;

    s->url = strdup(url);
    atomic_store(&s->running, 1);

    if (uv_thread_create(&s->thread, threadworker, s) != 0) {
        free(s->url);
        free(s);
        fprintf(stderr, "[ffmpeg] Failed to create decoder thread.\n");
        return NULL;
    }

    return s;
}

void stream_destroy(VideoStream *s) {
    if (!s) return;
    atomic_store(&s->running, 0);
    uv_thread_join(&s->thread);
    free(s->url);
    free(s);
}

MediaFrame* stream_get_frame(VideoStream *s) {
    if (!s || !atomic_load(&s->running)) return NULL;
    
    int front_idx = atomic_load(&s->front);
    MediaFrame *f = &s->frames[front_idx];

    if (f->pixels && atomic_load(&f->ready)) {
        return f;
    }
    return NULL;
}
