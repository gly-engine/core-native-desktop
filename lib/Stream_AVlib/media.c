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

// Translate FFmpeg format to GECND format
static int translate_format(int av_format) {
    if (av_format == AV_PIX_FMT_YUV420P) return GECND_PIX_FMT_YUV420P;
    if (av_format == AV_PIX_FMT_RGB565LE || av_format == AV_PIX_FMT_RGB565BE) return GECND_PIX_FMT_RGB565;
    return GECND_PIX_FMT_RGBA8888;
}

// Custom copy to handle potential padding issues.
static void frame_copy(AVFrame *src, MediaFrame *dst) {
    int num_planes = (dst->format == GECND_PIX_FMT_YUV420P) ? 3 : 1;
    for (int i = 0; i < num_planes; ++i) {
        if (!src->data[i] || !dst->data[i]) continue;
        
        int h = (i == 1 || i == 2) ? src->height / 2 : src->height;
        if (src->linesize[i] == dst->linesize[i]) {
            memcpy(dst->data[i], src->data[i], (size_t)(src->linesize[i] * h));
        } else {
            int dst_stride = dst->linesize[i];
            int src_stride = src->linesize[i];
            int copy_w = (i == 1 || i == 2) ? dst->width / 2 : dst->width;
            if (dst->format == GECND_PIX_FMT_RGBA8888) copy_w *= 4;
            else if (dst->format == GECND_PIX_FMT_RGB565) copy_w *= 2;
            
            for (int y = 0; y < h; ++y) {
                memcpy(dst->data[i] + y * dst_stride, src->data[i] + y * src_stride, (size_t)copy_w);
            }
        }
    }
}

static void threadworker(void *arg) {
    VideoStream *s = arg;

    AV.avformat_network_init();

    AVDictionary* opts = NULL;
    AV.av_dict_set(&opts, "timeout", "5000000", 0);
    AV.av_dict_set(&opts, "probesize", "50000000", 0);
    AV.av_dict_set(&opts, "analyzeduration", "10000000", 0);

    if (AV.avformat_open_input(&s->fmt, s->url, NULL, &opts) < 0) {
        fprintf(stderr, "[ffmpeg] Error opening file: %s\n", s->url);
        atomic_store(&s->running, 0);
        AV.av_dict_free(&opts);
        return;
    }
    AV.av_dict_free(&opts);

    if (AV.avformat_find_stream_info(s->fmt, NULL) < 0) {
        fprintf(stderr, "[ffmpeg] Error finding stream info\n");
        goto cleanup_format;
    }

    s->video_index = AV.av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (s->video_index < 0) {
        fprintf(stderr, "[ffmpeg] No video stream found\n");
        goto cleanup_format;
    }

    s->video = s->fmt->streams[s->video_index];
    const AVCodec *vdec = AV.avcodec_find_decoder(s->video->codecpar->codec_id);
    if (!vdec) goto cleanup_format;

    s->vcodec = AV.avcodec_alloc_context3(vdec);
    if (!s->vcodec) goto cleanup_format;
    AV.avcodec_parameters_to_context(s->vcodec, s->video->codecpar);
    if (AV.avcodec_open2(s->vcodec, vdec, NULL) < 0) goto cleanup_codec;

    AVPacket *pkt = AV.av_packet_alloc();
    AVFrame  *vfrm = AV.av_frame_alloc();
    bool initialized = false;
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
                    if (!initialized) {
                        gecnd_buffer_resize(vfrm->width, vfrm->height, translate_format(vfrm->format));
                        initialized = true;
                        
                        int64_t pts_first = vfrm->pts;
                        if (pts_first == AV_NOPTS_VALUE) pts_first = vfrm->best_effort_timestamp;
                        if (pts_first != AV_NOPTS_VALUE) {
                            s->clock_start = now_sec() - (pts_first * gly_av_q2d(s->video->time_base));
                        }
                    }

                    int64_t pts_val = vfrm->pts;
                    if (pts_val == AV_NOPTS_VALUE) pts_val = vfrm->best_effort_timestamp;
                    double pts = (pts_val != AV_NOPTS_VALUE) ? pts_val * gly_av_q2d(s->video->time_base) : 0.0;

                    double target = s->clock_start + pts;
                    double delay = target - now_sec();
                    if (delay > 0.001) usleep((useconds_t)(delay * 1e6));

                    MediaFrame *f = gecnd_buffer_get_back();
                    frame_copy(vfrm, f);
                    f->pts = pts;
                    atomic_store(&f->ready, true);
                    gecnd_buffer_swap();

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

VideoStream* stream_create(const char *url) {
    if (!av_load_ffmpeg()) return NULL;
    VideoStream *s = calloc(1, sizeof(VideoStream));
    if (!s) return NULL;
    s->url = strdup(url);
    atomic_store(&s->running, 1);
    atomic_store(&s->paused, 0);
    if (uv_thread_create(&s->thread, threadworker, s) != 0) {
        free(s->url); free(s); return NULL;
    }
    return s;
}

void stream_destroy(VideoStream *s) {
    if (!s) return;
    atomic_store(&s->running, 0);
    uv_thread_join(&s->thread);
    free(s->url); free(s);
}

MediaFrame* stream_get_frame(VideoStream *s) {
    (void)s;
    return gecnd_get_background_frame();
}
