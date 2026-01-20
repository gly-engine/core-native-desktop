#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <unistd.h>

#include <uv.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>

#include <raylib.h>

/* ===================================================== */
/* UTIL CLOCK                                           */
/* ===================================================== */

static inline double now_sec(void) {
    return av_gettime_relative() / 1e6;
}

/* ===================================================== */
/* FRAME / STREAM                                        */
/* ===================================================== */

typedef struct {
    uint8_t *pixels;   // RGBA
    int width;
    int height;
    int linesize;
    double pts;        // segundos
    atomic_int ready;
} VideoFrame;

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext  *codec;
    AVStream        *video;
    int video_index;
    struct SwsContext *sws;

    VideoFrame frames[2];
    atomic_int front;

    uv_thread_t thread;
    atomic_int running;

    double clock_start;
} VideoStream;

static VideoStream g_stream;

/* ===================================================== */
/* FRAME UTILS                                           */
/* ===================================================== */

static void frame_alloc(VideoFrame *f, int w, int h) {
    f->width = w;
    f->height = h;
    f->linesize = w * 4;
    f->pixels = malloc((size_t)f->linesize * h);
    atomic_store(&f->ready, 0);
}

static void frame_copy(
    struct SwsContext *sws,
    AVFrame *src,
    VideoFrame *dst
) {
    uint8_t *dst_data[4] = { dst->pixels, NULL, NULL, NULL };
    int dst_linesize[4] = { dst->linesize, 0, 0, 0 };

    sws_scale(
        sws,
        (const uint8_t * const*)src->data,
        src->linesize,
        0,
        src->height,
        dst_data,
        dst_linesize
    );
}

/* ===================================================== */
/* THREAD WORKER (libuv)                                 */
/* ===================================================== */

static void threadworker(void *arg) {
    VideoStream *s = arg;

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frm = av_frame_alloc();

    while (atomic_load(&s->running)) {
        if (av_read_frame(s->fmt, pkt) < 0)
            break;

        if (pkt->stream_index != s->video_index) {
            av_packet_unref(pkt);
            continue;
        }

        avcodec_send_packet(s->codec, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(s->codec, frm) == 0) {
            double pts =
                frm->best_effort_timestamp *
                av_q2d(s->video->time_base);

            double target_time = s->clock_start + pts;

            /* ---- SYNC PELO PTS (AQUI É O SEGREDO) ---- */
            for (;;) {
                double diff = target_time - now_sec();
                if (diff <= 0.0)
                    break;

                /* sleep curto pra não travar */
                if (diff > 0.01)
                    usleep(1000);
                else
                    usleep((useconds_t)(diff * 1e6));
            }
            /* ----------------------------------------- */

            int back = 1 - atomic_load(&s->front);
            VideoFrame *f = &s->frames[back];

            frame_copy(s->sws, frm, f);

            f->pts = pts;
            atomic_store(&f->ready, 1);
            atomic_store(&s->front, back);
        }
    }

    av_frame_free(&frm);
    av_packet_free(&pkt);
}

/* ===================================================== */
/* AVPLAY API                                           */
/* ===================================================== */

int avplay(const char *src) {
    VideoStream *s = &g_stream;

    avformat_open_input(&s->fmt, src, NULL, NULL);
    avformat_find_stream_info(s->fmt, NULL);

    s->video_index = av_find_best_stream(
        s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0
    );
    s->video = s->fmt->streams[s->video_index];

    const AVCodec *dec =
        avcodec_find_decoder(s->video->codecpar->codec_id);

    s->codec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(
        s->codec, s->video->codecpar
    );
    avcodec_open2(s->codec, dec, NULL);

    s->sws = sws_getContext(
        s->codec->width,
        s->codec->height,
        s->codec->pix_fmt,
        s->codec->width,
        s->codec->height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        NULL, NULL, NULL
    );

    frame_alloc(&s->frames[0],
        s->codec->width, s->codec->height);
    frame_alloc(&s->frames[1],
        s->codec->width, s->codec->height);

    atomic_store(&s->front, 0);
    atomic_store(&s->running, 1);

    s->clock_start = now_sec();

    uv_thread_create(&s->thread, threadworker, s);
    return 0;
}

VideoFrame *avplay_get_pixels(void) {
    VideoFrame *f =
        &g_stream.frames[atomic_load(&g_stream.front)];

    if (!atomic_load(&f->ready))
        return NULL;

    return f;
}

/* ===================================================== */
/* RAYLIB MAIN                                          */
/* ===================================================== */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("uso: %s video.mp4\n", argv[0]);
        return 0;
    }

    avplay(argv[1]);

    InitWindow(1280, 720, "FFmpeg + libuv + raylib (PTS sync)");
    SetTargetFPS(60);

    Texture2D tex = {0};
    int tex_ready = 0;

    while (!WindowShouldClose()) {
        VideoFrame *f = avplay_get_pixels();

        if (f && !tex_ready) {
            Image img = {
                .data = f->pixels,
                .width = f->width,
                .height = f->height,
                .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                .mipmaps = 1
            };
            tex = LoadTextureFromImage(img);
            tex_ready = 1;
        }

        if (f && tex_ready) {
            UpdateTexture(tex, f->pixels);
        }

        BeginDrawing();
        ClearBackground(BLACK);

        if (tex_ready) {
            DrawTexturePro(
                tex,
                (Rectangle){0,0,tex.width,tex.height},
                (Rectangle){0,0,GetScreenWidth(),GetScreenHeight()},
                (Vector2){0,0},
                0,
                WHITE
            );
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
