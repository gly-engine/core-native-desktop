#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gecnd.h"

#define GECND_STREAM_AVLIB_INTERNAL
#define GECND_FFMPEG_LOAD_INTERNAL
#include "gemedia.h"

static void normalize_url(char *url) {
    if (strncmp(url, "file://", 7) != 0) return;
    const char *path = url + 7;
    if (*path == '/') {
        memmove(url, path, strlen(path) + 1);      /* file:///x → /x */
    } else {
        memmove(url + 1, path, strlen(path) + 1);  /* file://x  → /x */
        url[0] = '/';
    }
}

static inline double now_sec(void) {
    return AV.av_gettime_relative() / 1e6;
}

static int translate_format(int av_format) {
    if (av_format == AV_PIX_FMT_YUV420P) return GECND_PIX_FMT_YUV420P;
    if (av_format == AV_PIX_FMT_RGB565LE || av_format == AV_PIX_FMT_RGB565BE) return GECND_PIX_FMT_RGB565;
    return GECND_PIX_FMT_RGBA8888;
}

static void threadworker(void *arg) {
    VideoStream *s = arg;
    AVPacket *pkt  = NULL;
    AVFrame  *vfrm = NULL;
    AV.avformat_network_init();

    AVDictionary *opts = NULL;
    AV.av_dict_set(&opts, "timeout", "5000000", 0);
    AV.av_dict_set(&opts, "tls_verify", "0", 0);

    if (AV.avformat_open_input(&s->fmt, s->url, NULL, &opts) < 0) {
        fprintf(stderr, "[media] error opening: %s\n", s->url);
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        atomic_store(&s->running, 0);
        AV.av_dict_free(&opts);
        return;
    }
    AV.av_dict_free(&opts);

    s->fmt->max_analyze_duration = 0;
    if (AV.avformat_find_stream_info(s->fmt, NULL) < 0) {
        fprintf(stderr, "[media] failed to find stream info: %s\n", s->url);
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_format;
    }

    s->video_index = AV.av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (s->video_index < 0) {
        fprintf(stderr, "[media] no video stream\n");
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_pkt_frame;
    }

    s->video = s->fmt->streams[s->video_index];
    const AVCodec *vdec = AV.avcodec_find_decoder(s->video->codecpar->codec_id);
    if (!vdec) {
        fprintf(stderr, "[media] no decoder for codec %d\n", s->video->codecpar->codec_id);
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_pkt_frame;
    }

    s->vcodec = AV.avcodec_alloc_context3(vdec);
    if (!s->vcodec) {
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_format;
    }
    AV.avcodec_parameters_to_context(s->vcodec, s->video->codecpar);

    if (s->vcodec->pix_fmt == AV_PIX_FMT_NONE)
        s->vcodec->pix_fmt = AV_PIX_FMT_YUV420P;
    s->vcodec->thread_count = 1;

    if (AV.avcodec_open2(s->vcodec, vdec, NULL) < 0) {
        fprintf(stderr, "[media] error opening codec\n");
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_codec;
    }

    pkt  = AV.av_packet_alloc();
    vfrm = AV.av_frame_alloc();
    bool initialized = false;
    s->clock_start = now_sec();

    AV.av_seek_frame(s->fmt, s->video_index, 0, AVSEEK_FLAG_BACKWARD);
    AV.avcodec_flush_buffers(s->vcodec);

    while (atomic_load(&s->running)) {
        if (AV.av_read_frame(s->fmt, pkt) < 0) {
            AV.av_seek_frame(s->fmt, s->video_index, 0, AVSEEK_FLAG_BACKWARD);
            AV.avcodec_flush_buffers(s->vcodec);
            s->clock_start = now_sec();
            continue;
        }

        if (pkt->stream_index == s->video_index) {
            if (AV.avcodec_send_packet(s->vcodec, pkt) == 0) {
                while (AV.avcodec_receive_frame(s->vcodec, vfrm) == 0) {
                    if (!initialized) {
                        initialized = true;
                        atomic_store(&s->state, GDMSP_FSM_PLAYING);
                        int64_t pts_first = vfrm->pts;
                        if (pts_first == AV_NOPTS_VALUE) pts_first = vfrm->best_effort_timestamp;
                        if (pts_first != AV_NOPTS_VALUE)
                            s->clock_start = now_sec() - (pts_first * gly_av_q2d(s->video->time_base));
                    }

                    int64_t pts_val = vfrm->pts;
                    if (pts_val == AV_NOPTS_VALUE) pts_val = vfrm->best_effort_timestamp;
                    double pts = (pts_val != AV_NOPTS_VALUE) ? pts_val * gly_av_q2d(s->video->time_base) : 0.0;

                    double delay = (s->clock_start + pts) - now_sec();
                    if (delay > 0.001) usleep((useconds_t)(delay * 1e6));

                    int fmt = translate_format(vfrm->format);
                    if (fmt == GECND_PIX_FMT_YUV420P) {
                        gamely_daemon_media_background_push_yuv420(
                            vfrm->data[0], vfrm->data[1], vfrm->data[2],
                            vfrm->width, vfrm->height,
                            vfrm->linesize[0], vfrm->linesize[1]);
                    } else if (fmt == GECND_PIX_FMT_RGB565) {
                        gamely_daemon_media_background_push_rgb565(
                            vfrm->data[0], vfrm->width, vfrm->height, vfrm->linesize[0]);
                    } else {
                        gamely_daemon_media_background_push_xrgb8888(
                            vfrm->data[0], vfrm->width, vfrm->height, vfrm->linesize[0]);
                    }

                    AV.av_frame_unref(vfrm);
                }
            }
        }
        AV.av_packet_unref(pkt);
    }

cleanup_pkt_frame:
    if (vfrm) AV.av_frame_free(&vfrm);
    if (pkt)  AV.av_packet_free(&pkt);
cleanup_codec:
    if (s->vcodec) AV.avcodec_free_context(&s->vcodec);
cleanup_format:
    if (s->fmt) AV.avformat_close_input(&s->fmt);
    AV.avformat_network_deinit();
}

VideoStream *stream_create(const char *url) {
    if (!av_load_ffmpeg()) return NULL;
    VideoStream *s = calloc(1, sizeof(VideoStream));
    if (!s) return NULL;
    s->url = strdup(url);
    normalize_url(s->url);
    atomic_store(&s->running, 1);
    atomic_store(&s->paused, 0);
    atomic_store(&s->state, GDMSP_FSM_LOADING);
    if (uv_thread_create(&s->thread, threadworker, s) != 0) {
        free(s->url); free(s); return NULL;
    }
    return s;
}

void stream_destroy(VideoStream *s) {
    if (!s) return;
    atomic_store(&s->state, GDMSP_FSM_STOPPING);
    atomic_store(&s->running, 0);
    uv_thread_join(&s->thread);
    free(s->url); free(s);
}

/* ── player callbacks ─────────────────────────────────────────────── */

static VideoStream *s_streams[4] = {0};

static void av_start(uint8_t channel, const char *url, void *usr) {
    (void)usr;
    if (channel >= 4) return;
    if (s_streams[channel]) {
        stream_destroy(s_streams[channel]);
        gamely_daemon_media_background_release();
        s_streams[channel] = NULL;
    }
    if (!gamely_daemon_media_background_claim()) {
        fprintf(stderr, "[media] background buffer in use\n");
        return;
    }
    s_streams[channel] = stream_create(url);
    if (!s_streams[channel]) gamely_daemon_media_background_release();
}

static void av_stop(uint8_t channel, void *usr) {
    (void)usr;
    if (channel >= 4 || !s_streams[channel]) return;
    stream_destroy(s_streams[channel]);
    s_streams[channel] = NULL;
    gamely_daemon_media_background_release();
}

static gdmsp_fsm_t av_state(uint8_t channel, void *usr) {
    (void)usr;
    if (channel >= 4 || !s_streams[channel]) return GDMSP_FSM_IDLE;
    return (gdmsp_fsm_t) atomic_load(&s_streams[channel]->state);
}

gamely_media_player_t gamely_player_ffmpeg = {
    .start = av_start,
    .stop  = av_stop,
    .tick  = NULL,
    .pause = NULL,
    .play  = NULL,
    .state = av_state,
};
