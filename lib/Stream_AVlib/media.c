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
    AV.av_dict_set(&opts, "timeout", "5000000", 0);
    AV.av_dict_set(&opts, "probesize", "50000000", 0);        // 50 MB
    AV.av_dict_set(&opts, "analyzeduration", "10000000", 0);  // 10 seconds

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

    AV.av_dump_format(s->fmt, 0, s->url, 0);

    s->video_index = AV.av_find_best_stream(
        s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    if (s->video_index < 0) {
        fprintf(stderr, "[ffmpeg] No video stream found\n");
        goto cleanup_format;
    }

    s->video = s->fmt->streams[s->video_index];

    fprintf(stderr, "\n--- VIDEO STREAM DEBUG ---\n");
    fprintf(stderr, "codec_id: %d\n", s->video->codecpar->codec_id);
    fprintf(stderr, "width: %d\n", s->video->codecpar->width);
    fprintf(stderr, "height: %d\n", s->video->codecpar->height);
    fprintf(stderr, "format: %d\n", s->video->codecpar->format);
    fprintf(stderr, "time_base: %d/%d\n",
            s->video->time_base.num,
            s->video->time_base.den);

    const AVCodec *vdec =
        AV.avcodec_find_decoder(s->video->codecpar->codec_id);

    if (!vdec) {
        fprintf(stderr, "[ffmpeg] Failed to find decoder\n");
        goto cleanup_format;
    }

    s->vcodec = AV.avcodec_alloc_context3(vdec);
    if (!s->vcodec) {
        fprintf(stderr, "[ffmpeg] Failed to alloc codec context\n");
        goto cleanup_format;
    }

    if (AV.avcodec_parameters_to_context(
            s->vcodec, s->video->codecpar) < 0) {
        fprintf(stderr, "[ffmpeg] Failed to copy codec parameters\n");
        goto cleanup_codec;
    }

    if (AV.avcodec_open2(s->vcodec, vdec, NULL) < 0) {
        fprintf(stderr, "[ffmpeg] Failed to open codec\n");
        goto cleanup_codec;
    }

    fprintf(stderr, "[ffmpeg] Decoder opened: %s\n", vdec->name);

    AVPacket *pkt = AV.av_packet_alloc();
    AVFrame  *vfrm = AV.av_frame_alloc();

    if (!pkt || !vfrm) {
        fprintf(stderr, "[ffmpeg] Failed to allocate packet/frame\n");
        goto cleanup_codec;
    }

    int frames_initialized = 0;
    atomic_store(&s->front, 0);
    s->clock_start = now_sec();

    while (atomic_load(&s->running)) {

        if (AV.av_read_frame(s->fmt, pkt) < 0) {
            AV.av_seek_frame(s->fmt, s->video_index, 0,
                             AVSEEK_FLAG_BACKWARD);
            s->clock_start = now_sec();
            continue;
        }

        if (pkt->stream_index == s->video_index) {

            if (AV.avcodec_send_packet(s->vcodec, pkt) == 0) {

                while (AV.avcodec_receive_frame(
                           s->vcodec, vfrm) == 0) {

                    if (!frames_initialized) {
                        fprintf(stderr,
                                "[ffmpeg] First frame received\n");
                        fprintf(stderr,
                                "Resolution: %dx%d\n",
                                vfrm->width, vfrm->height);
                        fprintf(stderr,
                                "Pixel format: %d\n",
                                vfrm->format);

                        frame_alloc(&s->frames[0],
                                    vfrm->width,
                                    vfrm->height,
                                    vfrm->format);

                        frame_alloc(&s->frames[1],
                                    vfrm->width,
                                    vfrm->height,
                                    vfrm->format);

                        frames_initialized = 1;
                    }

                    int64_t pts_val = vfrm->pts;

                    if (pts_val == AV_NOPTS_VALUE)
                        pts_val = vfrm->best_effort_timestamp;

                    if (pts_val == AV_NOPTS_VALUE)
                        pts_val = vfrm->pkt_dts;

                    double pts = 0.0;

                    if (pts_val != AV_NOPTS_VALUE) {
                        pts = pts_val *
                              gly_av_q2d(
                                  s->video->time_base);
                    }

                    double target =
                        s->clock_start + pts;

                    double delay =
                        target - now_sec();

                    if (delay > 0.001)
                        usleep((useconds_t)
                                (delay * 1e6));

                    if (frames_initialized) {
                        int back =
                            1 - atomic_load(&s->front);

                        MediaFrame *f =
                            &s->frames[back];

                        frame_copy(vfrm, f);

                        f->pts = pts;

                        atomic_store(&f->ready,
                                     true);

                        atomic_store(&s->front,
                                     back);
                    }

                    AV.av_frame_unref(vfrm);
                }
            }
        }

        AV.av_packet_unref(pkt);
    }

    AV.av_frame_free(&vfrm);
    AV.av_packet_free(&pkt);

    if (frames_initialized) {
        frame_free(&s->frames[0]);
        frame_free(&s->frames[1]);
    }

cleanup_codec:
    if (s->vcodec)
        AV.avcodec_free_context(&s->vcodec);

cleanup_format:
    if (s->fmt)
        AV.avformat_close_input(&s->fmt);

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
