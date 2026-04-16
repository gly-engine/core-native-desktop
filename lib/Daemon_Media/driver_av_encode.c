#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GECND_FFMPEG_LOAD_INTERNAL
#define GECND_STREAM_AVLIB_INTERNAL
#include "gemedia.h"

typedef void (*encode_ts_cb)(const uint8_t *, int, int64_t pts);

/* -----------------------------------------------------------------------
 * Contextos FFmpeg
 * ---------------------------------------------------------------------- */
static AVCodecContext  *g_ctx    = NULL;
static AVFormatContext *g_fmt    = NULL;
static AVStream        *g_stream = NULL;
static AVFrame         *g_frame  = NULL;
static AVPacket        *g_pkt    = NULL;

static encode_ts_cb     g_on_ts  = NULL;
static int64_t          g_cur_pts = 0;
static int64_t          g_pts    = 0;

/* -----------------------------------------------------------------------
 * GOP cache — captura o IDR inteiro + todos os P-frames até "agora".
 * Enviado a novos clientes para início imediato e sem gaps de frame_num.
 *
 * Tamanho: 720p ultrafast ~2Mbps × 0.5s (GOP=15@30fps) ≈ 125 KB de H.264
 * + overhead MPEG-TS (~5%) + PAT/PMT (resend_headers) ≈ 200 KB no pior caso.
 * 1 MB dá margem para 4×4 Mbps sem problema.
 * ---------------------------------------------------------------------- */
#define GOP_CACHE_MAX (1 * 1024 * 1024)   /* 1 MB */
static uint8_t g_gop_buf[GOP_CACHE_MAX];
static int     g_gop_len     = 0;
static int     g_gop_active  = 0;  /* 1: acumulando frames desde o último IDR */
static int     g_gop_ready   = 0;  /* 1: há pelo menos um IDR no cache       */

/* flag: dentro de avformat_write_header (para não poluir o GOP cache) */
static int     g_in_header   = 0;

/* -----------------------------------------------------------------------
 * Força IDR no próximo encode_push
 * ---------------------------------------------------------------------- */
static int g_force_idr = 0;

/* -----------------------------------------------------------------------
 * AVIO write callback — acumula GOP cache e despacha para o serviço
 * ---------------------------------------------------------------------- */
static int ts_write_cb(void *opaque, uint8_t *buf, int size) {
    (void)opaque;
    /* acumula no GOP cache (IDR + todos P-frames até o momento) */
    if (g_gop_active && !g_in_header) {
        if (g_gop_len + size <= GOP_CACHE_MAX) {
            memcpy(g_gop_buf + g_gop_len, buf, size);
            g_gop_len += size;
        } else {
            /* cache cheio: GOP maior que esperado — descarta e aguarda próximo IDR */
            g_gop_active = 0;
            g_gop_ready  = 0;
            g_gop_len    = 0;
        }
    }
    if (g_on_ts) g_on_ts((const uint8_t *)buf, size, g_cur_pts);
    return size;
}

/* -----------------------------------------------------------------------
 * RGB → YUV420 com flip vertical (OpenGL: origem no canto inferior-esquerdo)
 * ---------------------------------------------------------------------- */
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

/* -----------------------------------------------------------------------
 * drain_encoder — recebe pacotes do codec e multiplexia no TS.
 *
 * GOP cache: no IDR, reseta o buffer e começa a acumular; nos P/B-frames,
 * continua acumulando. Assim o cache sempre contém [PAT+PMT + IDR + P…]
 * i.e., o GOP completo até "agora", que é exatamente o que um novo cliente
 * precisa para decodificar sem gaps de frame_num.
 * ---------------------------------------------------------------------- */
static void drain_encoder(void) {
    int ret;
    while ((ret = AV.avcodec_receive_packet(g_ctx, g_pkt)) >= 0) {
        g_cur_pts = g_pkt->pts;
        g_pkt->stream_index = g_stream->index;

        int is_key = (g_pkt->flags & AV_PKT_FLAG_KEY) != 0;
        if (is_key) {
            /* novo IDR: reinicia o GOP cache (inclui PAT+PMT via resend_headers) */
            g_gop_len    = 0;
            g_gop_active = 1;
            g_gop_ready  = 0;
        }

        AV.av_interleaved_write_frame(g_fmt, g_pkt);
        /* g_gop_active permanece 1 para os P-frames seguintes */

        if (is_key)
            g_gop_ready = 1; /* IDR já está no cache — partials são utilizáveis */

        AV.av_packet_unref(g_pkt);
    }
    if (g_fmt && g_fmt->pb) AV.avio_flush(g_fmt->pb);
}

/* -----------------------------------------------------------------------
 * encode_init
 * ---------------------------------------------------------------------- */
bool encode_init(int w, int h, int fps, encode_ts_cb on_ts) {
    if (!av_load_ffmpeg()) return false;

    g_on_ts      = on_ts;
    g_pts        = 0;
    g_force_idr  = 0;
    g_gop_len    = 0;
    g_gop_active = 0;
    g_gop_ready  = 0;

    const AVCodec *codec = AV.avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return false;

    g_ctx = AV.avcodec_alloc_context3(codec);
    if (!g_ctx) return false;

    g_ctx->width  = w;
    g_ctx->height = h;
    AV.av_opt_set_q  (g_ctx, "time_base", (AVRational){1,   fps}, 0);
    AV.av_opt_set_q  (g_ctx, "framerate", (AVRational){fps, 1},   0);
    AV.av_opt_set_int(g_ctx, "g",  (int64_t)(fps / 2), 0); /* GOP = fps/2 → IDR a cada 0.5s */
    AV.av_opt_set_int(g_ctx, "bf", 0,                  0); /* sem B-frames */
    if (AV.av_opt_set(g_ctx, "pixel_format", "yuv420p", 0) < 0) {
        fprintf(stderr, "[encode] falha ao setar pixel_format\n");
        AV.avcodec_free_context(&g_ctx);
        return false;
    }

    AVDictionary *opts = NULL;
    AV.av_dict_set(&opts, "preset",      "ultrafast",        0);
    AV.av_dict_set(&opts, "tune",        "zerolatency",      0);
    /* SPS+PPS embutidos em todo IDR — clientes mid-stream conseguem decodificar */
    AV.av_dict_set(&opts, "x264-params", "repeat-headers=1", 0);

    if (AV.avcodec_open2(g_ctx, codec, &opts) < 0) {
        AV.av_dict_free(&opts);
        AV.avcodec_free_context(&g_ctx);
        return false;
    }
    AV.av_dict_free(&opts);

    AV.avformat_alloc_output_context2(&g_fmt, NULL, "mpegts", NULL);
    g_fmt->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_FLUSH_PACKETS;

    /* PAT+PMT antes de cada IDR: clientes que conectam mid-stream recebem
     * tabelas no próximo keyframe (cobre o caso sem IDR cache ainda) */
    AV.av_opt_set(g_fmt->priv_data, "mpegts_flags", "resend_headers", 0);

    g_stream = AV.avformat_new_stream(g_fmt, NULL);
    g_stream->time_base = (AVRational){1, fps};
    AV.avcodec_parameters_from_context(g_stream->codecpar, g_ctx);

    uint8_t *io_buf = AV.av_malloc(3760); /* 20 × 188 */
    g_fmt->pb = AV.avio_alloc_context(io_buf, 3760, 1, NULL, NULL, ts_write_cb, NULL);

    /* captura PAT+PMT inicial como welcome packet */
    g_in_header = 1;
    AV.avformat_write_header(g_fmt, NULL);
    g_in_header = 0;

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

    g_pkt = AV.av_packet_alloc();
    return true;
}

/* -----------------------------------------------------------------------
 * encode_push — síncrono, mesma thread do loop libuv/GL.
 * Sem thread separada: sem mutex, sem cond, sem latência de signaling.
 * ---------------------------------------------------------------------- */
void encode_push(const uint8_t *rgba, int w, int h) {
    if (!g_ctx || w != g_ctx->width || h != g_ctx->height) return;

    rgba_to_yuv420(rgba, w, h);

    g_frame->pts = g_pts++;
    if (g_force_idr) {
        g_frame->pict_type = AV_PICTURE_TYPE_I;
        g_frame->key_frame = 1;
        g_force_idr        = 0;
    } else {
        g_frame->pict_type = AV_PICTURE_TYPE_NONE;
        g_frame->key_frame = 0;
    }

    AV.avcodec_send_frame(g_ctx, g_frame);
    drain_encoder();
}

/* -----------------------------------------------------------------------
 * encode_force_idr — próximo encode_push emitirá IDR
 * ---------------------------------------------------------------------- */
void encode_force_idr(void) {
    g_force_idr = 1;
}

/* -----------------------------------------------------------------------
 * encode_get_idr_cache — GOP cache atual: PAT+PMT + IDR + P-frames até agora.
 * Retorna o comprimento; *out aponta para buffer interno estático.
 * Válido até o próximo encode_push (single-thread: sem race condition).
 * ---------------------------------------------------------------------- */
int encode_get_idr_cache(const uint8_t **out) {
    if (g_gop_ready && g_gop_len > 0) {
        if (out) *out = g_gop_buf;
        return g_gop_len;
    }
    if (out) *out = NULL;
    return 0;
}

/* -----------------------------------------------------------------------
 * encode_shutdown
 * ---------------------------------------------------------------------- */
void encode_shutdown(void) {
    if (!g_ctx) return;

    AV.avcodec_send_frame(g_ctx, NULL);
    drain_encoder();
    AV.av_write_trailer(g_fmt);

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

    g_stream     = NULL;
    g_gop_len    = 0;
    g_gop_active = 0;
    g_gop_ready  = 0;
    g_force_idr  = 0;
}
