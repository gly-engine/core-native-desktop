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

static void frame_alloc(MediaFrame *f, int w, int h, int format) {
    f->width = w;
    f->height = h;
    f->format = format;
    
    int size = AV.av_image_get_buffer_size(format, w, h, 1);
    if (size < 0) {
        fprintf(stderr, "[ffmpeg] Failed to get buffer size for format %d\n", format);
        f->data[0] = NULL;
        return;
    }
    f->data[0] = malloc(size);
    AV.av_image_fill_arrays(f->data, f->linesize, f->data[0], format, w, h, 1);
    
    atomic_store(&f->ready, false);
}

static void frame_free(MediaFrame *f) {
    if(f->data[0]) {
        free(f->data[0]);
    }
    memset(f->data, 0, sizeof(f->data));
    memset(f->linesize, 0, sizeof(f->linesize));
}

// Custom copy to handle potential padding issues.
static void frame_copy(AVFrame *src, MediaFrame *dst) {
    // av_image_copy_to_buffer is a good candidate, but let's do it manually
    // to be sure about plane layout.
    for (int i = 0; i < 4; ++i) {
        if (!src->data[i] || !dst->data[i]) continue;
        
        int h = (i == 1 || i == 2) ? src->height / 2 : src->height;
        if (src->linesize[i] == dst->linesize[i]) {
            memcpy(dst->data[i], src->data[i], src->linesize[i] * h);
        } else {
            // Copy row by row if linesizes are different
            for (int y = 0; y < h; ++y) {
                memcpy(dst->data[i] + y * dst->linesize[i], src->data[i] + y * src->linesize[i], dst->linesize[i]);
            }
        }
    }
}

static void threadworker(void *arg) {
    VideoStream *s = arg;

    AV.avformat_network_init();

    AVDictionary* opts = NULL;
    AV.av_dict_set(&opts, "timeout", "5000000", 0); // 5 seconds timeout

    if (AV.avformat_open_input(&s->fmt, s->url, NULL, &opts) < 0) {
        fprintf(stderr, "[ffmpeg] Error opening file: %s\n", s->url);
        atomic_store(&s->running, 0);
        AV.av_dict_free(&opts);
        return;
    }
    AV.av_dict_free(&opts);

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
    if (!vdec) {
        fprintf(stderr, "[ffmpeg] Failed to find codec id %d\n", s->video->codecpar->codec_id);
        goto cleanup_format;
    }
    s->vcodec = AV.avcodec_alloc_context3(vdec);
    AV.avcodec_parameters_to_context(s->vcodec, s->video->codecpar);
    if(AV.avcodec_open2(s->vcodec, vdec, NULL) < 0) {
        fprintf(stderr, "[ffmpeg] Failed to open codec for %s\n", s->url);
        goto cleanup_codec;
    }

    frame_alloc(&s->frames[0], s->vcodec->width, s->vcodec->height, s->vcodec->pix_fmt);
    frame_alloc(&s->frames[1], s->vcodec->width, s->vcodec->height, s->vcodec->pix_fmt);
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
                    if (pts_val == AV_NOPTS_VALUE) pts_val = vfrm->best_effort_timestamp;
                    if (pts_val == AV_NOPTS_VALUE) pts_val = vfrm->pkt_dts;

                    double pts = (pts_val == AV_NOPTS_VALUE) ? 0 : pts_val * gly_av_q2d(s->video->time_base);

                    double target_time = s->clock_start + pts;
                    double delay = target_time - now_sec();
                    if (delay > 0.001) usleep((useconds_t)(delay * 1e6));
                    
                    int back = 1 - atomic_load(&s->front);
                    MediaFrame *f = &s->frames[back];
                    frame_copy(vfrm, f);
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

    if (f->data[0] && atomic_load(&f->ready)) {
        return f;
    }
    return NULL;
}
