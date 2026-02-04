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
#include <libavutil/channel_layout.h>

#include <raylib.h>

/* ===================================================== */
/* UTIL CLOCK                                           */
/* ===================================================== */
static inline double now_sec(void) {
    return av_gettime_relative() / 1e6;
}

/* ===================================================== */
/* VIDEO / AUDIO STREAM                                  */
/* ===================================================== */

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
    int linesize;
    double pts;
    atomic_int ready;
} VideoFrame;

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext  *vcodec;
    AVCodecContext  *acodec;
    AVStream        *video;
    AVStream        *audio;
    int video_index;
    int audio_index;
    struct SwsContext *sws;

    VideoFrame frames[2];
    atomic_int front;

    uv_thread_t thread;
    atomic_int running;
    double clock_start;

    // Audio
    AudioStream ray_audio;
    float *audio_buf;
    int audio_buf_size;
    int audio_buf_pos;
    int audio_channels;
} VideoStream;

static VideoStream g_stream;

/* ===================================================== */
/* FRAME UTIL                                           */
/* ===================================================== */
static void frame_alloc(VideoFrame *f, int w, int h) {
    f->width = w;
    f->height = h;
    f->linesize = w * 4;
    f->pixels = malloc((size_t)f->linesize * h);
    atomic_store(&f->ready, 0);
}

static void frame_copy(struct SwsContext *sws, AVFrame *src, VideoFrame *dst) {
    uint8_t *dst_data[4] = { dst->pixels, NULL, NULL, NULL };
    int dst_linesize[4] = { dst->linesize, 0, 0, 0 };
    sws_scale(sws, (const uint8_t * const*)src->data, src->linesize, 0, src->height, dst_data, dst_linesize);
}

/* ===================================================== */
/* THREAD WORKER (LEITURA DE PACOTES + DECODIFICAÇÃO)   */
/* ===================================================== */

static void threadworker(void *arg) {
    VideoStream *s = arg;

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *vfrm = av_frame_alloc();
    AVFrame  *afrm = av_frame_alloc();

    while (atomic_load(&s->running)) {
        if (av_read_frame(s->fmt, pkt) < 0)
            break;

        if (pkt->stream_index == s->video_index) {
            // Decodifica vídeo
            if (avcodec_send_packet(s->vcodec, pkt) == 0) {
                while (avcodec_receive_frame(s->vcodec, vfrm) == 0) {
                    double pts = vfrm->best_effort_timestamp * av_q2d(s->video->time_base);
                    double target_time = s->clock_start + pts;

                    while ((target_time - now_sec()) > 0.0) {
                        double diff = target_time - now_sec();
                        if (diff > 0.01) usleep(1000);
                        else usleep((useconds_t)(diff*1e6));
                    }

                    int back = 1 - atomic_load(&s->front);
                    VideoFrame *f = &s->frames[back];
                    frame_copy(s->sws, vfrm, f);
                    f->pts = pts;
                    atomic_store(&f->ready, 1);
                    atomic_store(&s->front, back);
                }
            }
        }
        else if (pkt->stream_index == s->audio_index) {
            // Decodifica áudio
            if (avcodec_send_packet(s->acodec, pkt) == 0) {
                while (avcodec_receive_frame(s->acodec, afrm) == 0) {
                    int nb_samples = afrm->nb_samples;
                    int channels = afrm->ch_layout.nb_channels;

                    for (int i = 0; i < nb_samples; i++) {
                        for (int c = 0; c < channels; c++) {
                            int16_t *data = (int16_t*)afrm->data[c]; // S16
                            float sample = data[i] / 32768.0f;
                            s->audio_buf[s->audio_buf_pos++] = sample;

                            // envia se buffer cheio
                            if (s->audio_buf_pos >= s->audio_buf_size) {
                                while (!IsAudioStreamProcessed(s->ray_audio))
                                    usleep(1000);
                                UpdateAudioStream(s->ray_audio, s->audio_buf, s->audio_buf_pos);
                                s->audio_buf_pos = 0;
                            }
                        }
                    }
                }
            }
        }

        av_packet_unref(pkt);
    }

    // envia restos de áudio
    if (s->audio_buf_pos > 0) {
        while (!IsAudioStreamProcessed(s->ray_audio))
                                                usleep(1000);

        UpdateAudioStream(s->ray_audio, s->audio_buf, s->audio_buf_pos);
        s->audio_buf_pos = 0;
    }

    av_frame_free(&vfrm);
    av_frame_free(&afrm);
    av_packet_free(&pkt);
}

/* ===================================================== */
/* AVPLAY API                                           */
/* ===================================================== */

int avplay(const char *src) {
    VideoStream *s = &g_stream;

    if (avformat_open_input(&s->fmt, src, NULL, NULL) < 0) {
        fprintf(stderr,"Erro ao abrir arquivo: %s\n", src);
        return -1;
    }
    if (avformat_find_stream_info(s->fmt, NULL) < 0) {
        fprintf(stderr,"Erro ao encontrar stream info\n");
        return -1;
    }

    // VIDEO
    s->video_index = av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    s->video = s->fmt->streams[s->video_index];
    const AVCodec *vdec = avcodec_find_decoder(s->video->codecpar->codec_id);
    s->vcodec = avcodec_alloc_context3(vdec);
    avcodec_parameters_to_context(s->vcodec, s->video->codecpar);
    avcodec_open2(s->vcodec, vdec, NULL);

    // AUDIO
    s->audio_index = av_find_best_stream(s->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (s->audio_index >= 0) {
        s->audio = s->fmt->streams[s->audio_index];
        const AVCodec *adec = avcodec_find_decoder(s->audio->codecpar->codec_id);
        s->acodec = avcodec_alloc_context3(adec);
        avcodec_parameters_to_context(s->acodec, s->audio->codecpar);
        avcodec_open2(s->acodec, adec, NULL);

        InitAudioDevice();

        s->audio_channels = s->audio->codecpar->ch_layout.nb_channels;
        int audio_rate = s->audio->codecpar->sample_rate;

        s->ray_audio = LoadAudioStream(s->audio_channels, audio_rate, 32);
        PlayAudioStream(s->ray_audio);

        s->audio_buf_size = 8192 * s->audio_channels; // buffer maior
        s->audio_buf = malloc(sizeof(float)*s->audio_buf_size);
        s->audio_buf_pos = 0;
    }

    // SWS
    s->sws = sws_getContext(
        s->vcodec->width, s->vcodec->height, s->vcodec->pix_fmt,
        s->vcodec->width, s->vcodec->height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, NULL, NULL, NULL
    );

    frame_alloc(&s->frames[0], s->vcodec->width, s->vcodec->height);
    frame_alloc(&s->frames[1], s->vcodec->width, s->vcodec->height);

    atomic_store(&s->front,0);
    atomic_store(&s->running,1);
    s->clock_start = now_sec();

    uv_thread_create(&s->thread, threadworker, s);

    return 0;
}

VideoFrame *avplay_get_pixels(void) {
    VideoFrame *f = &g_stream.frames[atomic_load(&g_stream.front)];
    if (!atomic_load(&f->ready)) return NULL;
    return f;
}

/* ===================================================== */
/* MAIN RAYLIB                                          */
/* ===================================================== */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("uso: %s video.mp4\n", argv[0]);
        return 0;
    }

    if (avplay(argv[1]) < 0)
        return -1;

    InitWindow(1280,720,"FFmpeg + Raylib (PTS sync)");
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

        if (f && tex_ready)
            UpdateTexture(tex, f->pixels);

        BeginDrawing();
        ClearBackground(BLACK);
        if (tex_ready)
            DrawTexturePro(tex,
                (Rectangle){0,0,tex.width,tex.height},
                (Rectangle){0,0,GetScreenWidth(),GetScreenHeight()},
                (Vector2){0,0},0,WHITE
            );
        EndDrawing();
    }

    atomic_store(&g_stream.running,0);
    uv_thread_join(&g_stream.thread);

    CloseAudioDevice();
    CloseWindow();

    free(g_stream.audio_buf);
    free(g_stream.frames[0].pixels);
    free(g_stream.frames[1].pixels);

    return 0;
}
