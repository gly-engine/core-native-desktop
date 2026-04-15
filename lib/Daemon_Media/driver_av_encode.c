#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>

#define GECND_FFMPEG_LOAD_INTERNAL
#include "gemedia.h"

typedef void (*encode_ts_cb)(const uint8_t *, int);

static AVCodecContext  *g_ctx       = NULL;
static AVFormatContext *g_fmt       = NULL;
static AVStream        *g_stream    = NULL;
static AVFrame         *g_frame     = NULL;
static AVPacket        *g_pkt       = NULL;

static uint8_t         *g_slot      = NULL;
static int              g_slot_w    = 0;
static int              g_slot_h    = 0;

static pthread_t        g_thread;
static pthread_mutex_t  g_mutex     = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_cond      = PTHREAD_COND_INITIALIZER;
static atomic_int       g_running   = 0;
static atomic_int       g_slot_seq  = 0;
static int              g_last_seq  = -1;
static int64_t          g_pts       = 0;

static encode_ts_cb     g_on_ts     = NULL;

static int ts_write_cb(void *opaque, uint8_t *buf, int size) {
    (void)opaque;
    if (g_on_ts) g_on_ts((const uint8_t *)buf, size);
    return size;
}

static void rgba_to_yuv420(const uint8_t *rgba, int w, int h) {
    int ys = g_frame->linesize[0];
    int us = g_frame->linesize[1];
    int vs = g_frame->linesize[2];

    for (int row = 0; row < h; row++) {
        const uint8_t *src = rgba + (h - 1 - row) * w * 4;
        uint8_t *dst_y = g_frame->data[0] + row * ys;
        for (int col = 0; col < w; col++) {
            int r = src[col*4], g = src[col*4+1], b = src[col*4+2];
            dst_y[col] = (uint8_t)(((66*r + 129*g + 25*b + 128) >> 8) + 16);
        }
    }
    for (int row = 0; row < h/2; row++) {
        const uint8_t *s0 = rgba + (h - 1 - row*2)       * w * 4;
        const uint8_t *s1 = rgba + (h - 1 - (row*2 + 1)) * w * 4;
        uint8_t *dst_u = g_frame->data[1] + row * us;
        uint8_t *dst_v = g_frame->data[2] + row * vs;
        for (int col = 0; col < w/2; col++) {
            int r = ((int)s0[col*8] + s0[col*8+4] + s1[col*8] + s1[col*8+4]) >> 2;
            int g = ((int)s0[col*8+1] + s0[col*8+5] + s1[col*8+1] + s1[col*8+5]) >> 2;
            int b = ((int)s0[col*8+2] + s0[col*8+6] + s1[col*8+2] + s1[col*8+6]) >> 2;
            dst_u[col] = (uint8_t)(((-38*r -  74*g + 112*b + 128) >> 8) + 128);
            dst_v[col] = (uint8_t)(((112*r -  94*g -  18*b + 128) >> 8) + 128);
        }
    }
}

static void drain_encoder(void) {
    int ret;
    while ((ret = AV.avcodec_receive_packet(g_ctx, g_pkt)) >= 0) {
        g_pkt->stream_index = g_stream->index;
        AV.av_interleaved_write_frame(g_fmt, g_pkt);
        AV.av_packet_unref(g_pkt);
    }
}

static void *encode_thread(void *arg) {
    (void)arg;
    uint8_t *local = NULL;

    while (1) {
        pthread_mutex_lock(&g_mutex);
        while (atomic_load(&g_running) && atomic_load(&g_slot_seq) == g_last_seq)
            pthread_cond_wait(&g_cond, &g_mutex);

        if (!atomic_load(&g_running)) {
            pthread_mutex_unlock(&g_mutex);
            break;
        }

        int seq = atomic_load(&g_slot_seq);
        int w = g_slot_w, h = g_slot_h;

        if (!local) local = malloc((size_t)(w * h * 4));
        memcpy(local, g_slot, (size_t)(w * h * 4));
        g_last_seq = seq;
        pthread_mutex_unlock(&g_mutex);

        rgba_to_yuv420(local, w, h);
        g_frame->pts = g_pts++;
        AV.avcodec_send_frame(g_ctx, g_frame);
        drain_encoder();
    }

    AV.avcodec_send_frame(g_ctx, NULL);
    drain_encoder();
    AV.av_write_trailer(g_fmt);

    free(local);
    return NULL;
}

bool encode_init(int w, int h, int fps, encode_ts_cb on_ts) {
    if (!av_load_ffmpeg()) return false;

    g_on_ts    = on_ts;
    g_pts      = 0;
    g_last_seq = -1;

    const AVCodec *codec = AV.avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return false;

    g_ctx = AV.avcodec_alloc_context3(codec);
    g_ctx->width       = w;
    g_ctx->height      = h;
    g_ctx->pix_fmt     = AV_PIX_FMT_YUV420P;
    g_ctx->time_base   = (AVRational){1, fps};
    g_ctx->framerate   = (AVRational){fps, 1};
    g_ctx->gop_size    = fps;
    g_ctx->max_b_frames = 0;

    AVDictionary *opts = NULL;
    AV.av_dict_set(&opts, "preset", "ultrafast", 0);
    AV.av_dict_set(&opts, "tune",   "zerolatency", 0);

    if (AV.avcodec_open2(g_ctx, codec, &opts) < 0) {
        AV.av_dict_free(&opts);
        AV.avcodec_free_context(&g_ctx);
        return false;
    }
    AV.av_dict_free(&opts);

    AV.avformat_alloc_output_context2(&g_fmt, NULL, "mpegts", NULL);
    g_fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    g_stream = AV.avformat_new_stream(g_fmt, NULL);
    g_stream->time_base = (AVRational){1, fps};
    AV.avcodec_parameters_from_context(g_stream->codecpar, g_ctx);

    uint8_t *io_buf = AV.av_malloc(4096);
    g_fmt->pb = AV.avio_alloc_context(io_buf, 4096, 1, NULL, NULL, ts_write_cb, NULL);

    AV.avformat_write_header(g_fmt, NULL);

    g_frame = AV.av_frame_alloc();
    g_frame->format = AV_PIX_FMT_YUV420P;
    g_frame->width  = w;
    g_frame->height = h;
    g_frame->linesize[0] = w;
    g_frame->linesize[1] = w / 2;
    g_frame->linesize[2] = w / 2;
    g_frame->data[0] = malloc((size_t)(w * h));
    g_frame->data[1] = malloc((size_t)(w / 2 * h / 2));
    g_frame->data[2] = malloc((size_t)(w / 2 * h / 2));

    g_pkt  = AV.av_packet_alloc();
    g_slot = malloc((size_t)(w * h * 4));
    g_slot_w = w;
    g_slot_h = h;

    atomic_store(&g_slot_seq, 0);
    atomic_store(&g_running, 1);
    pthread_create(&g_thread, NULL, encode_thread, NULL);
    return true;
}

void encode_push(const uint8_t *rgba, int w, int h) {
    if (!g_ctx || !g_slot || w != g_slot_w || h != g_slot_h) return;
    pthread_mutex_lock(&g_mutex);
    memcpy(g_slot, rgba, (size_t)(w * h * 4));
    atomic_fetch_add(&g_slot_seq, 1);
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
}

void encode_shutdown(void) {
    if (!atomic_load(&g_running)) return;

    pthread_mutex_lock(&g_mutex);
    atomic_store(&g_running, 0);
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
    pthread_join(g_thread, NULL);

    if (g_frame) {
        free(g_frame->data[0]);
        free(g_frame->data[1]);
        free(g_frame->data[2]);
        AV.av_frame_free(&g_frame);
    }
    if (g_pkt) AV.av_packet_free(&g_pkt);

    if (g_fmt) {
        if (g_fmt->pb) {
            AV.av_free(g_fmt->pb->buffer);
            AV.av_free(g_fmt->pb);
            g_fmt->pb = NULL;
        }
        AV.avformat_free_context(g_fmt);
        g_fmt = NULL;
    }
    if (g_ctx) {
        AV.avcodec_free_context(&g_ctx);
        g_ctx = NULL;
    }

    free(g_slot);
    g_slot   = NULL;
    g_slot_w = 0;
    g_slot_h = 0;
    g_stream = NULL;
}
