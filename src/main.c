// SPDX-License-Identifier: Unlicense

#include <raylib.h>
#include <uv.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --- Constants and Definitions ---
#define MAX_QUEUE_SIZE 240
#define VIDEO_FILENAME "/home/rodrigao/Videos/reddresswoman.mp4"

// --- Thread-Safe Queue for AVFrame* ---
typedef struct FrameQueue {
    AVFrame* frames[MAX_QUEUE_SIZE];
    int size;
    int head;
    int tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} FrameQueue;

void queue_init(FrameQueue* q) {
    memset(q, 0, sizeof(FrameQueue));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

void queue_push(FrameQueue* q, AVFrame* frame) {
    pthread_mutex_lock(&q->mutex);
    while (q->size >= MAX_QUEUE_SIZE) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    q->frames[q->head] = frame;
    q->head = (q->head + 1) % MAX_QUEUE_SIZE;
    q->size++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

AVFrame* queue_pop(FrameQueue* q) {
    pthread_mutex_lock(&q->mutex);
    while (q->size <= 0) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    AVFrame* frame = q->frames[q->tail];
    q->tail = (q->tail + 1) % MAX_QUEUE_SIZE;
    q->size--;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return frame;
}

AVFrame* queue_peek(FrameQueue* q) {
    pthread_mutex_lock(&q->mutex);
    AVFrame* frame = NULL;
    if (q->size > 0) {
        frame = q->frames[q->tail];
    }
    pthread_mutex_unlock(&q->mutex);
    return frame;
}

void queue_destroy(FrameQueue* q) {
    while(q->size > 0) {
        AVFrame* frame = queue_pop(q);
        av_frame_free(&frame);
    }
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}


// --- Player State ---
typedef struct PlayerState {
    // FFmpeg
    AVFormatContext* format_ctx;
    AVCodecContext* video_codec_ctx;
    AVCodecContext* audio_codec_ctx;
    int video_stream_idx;
    int audio_stream_idx;
    struct SwsContext* sws_ctx;
    struct SwrContext* swr_ctx;
    AVRational video_time_base;

    // Queues
    FrameQueue video_q;
    FrameQueue audio_q;

    // libuv
    uv_loop_t* loop;
    uv_work_t decode_req;
    uv_async_t eof_async;
    int eof;

    // Raylib
    Texture2D video_texture;
    AudioStream audio_stream;
    Image video_image;

    // Sync
    double audio_clock;
    double last_video_pts;

} PlayerState;

// --- Global State ---
// Made global to be accessible by the audio callback which has a fixed signature.
PlayerState* g_ps = NULL;

// --- Audio Clock ---
void update_audio_clock(double pts) {
    if (g_ps) g_ps->audio_clock = pts;
}

double get_audio_clock() {
    return g_ps ? g_ps->audio_clock : 0.0;
}

// --- Raylib Audio Callback ---
void audio_callback(void* buffer, unsigned int frames) {
    if (!g_ps || !g_ps->swr_ctx) return;

    float* out = (float*)buffer;
    int frames_decoded = 0;

    while (frames_decoded < frames) {
        AVFrame* audio_frame = queue_pop(&g_ps->audio_q);
        if (!audio_frame) return;

        uint8_t* resampled_data[1] = { (uint8_t*)out + frames_decoded * g_ps->audio_codec_ctx->ch_layout.nb_channels * sizeof(float) };
        int samples_per_channel = swr_convert(g_ps->swr_ctx, resampled_data, frames - frames_decoded, (const uint8_t**)audio_frame->data, audio_frame->nb_samples);

        if (samples_per_channel > 0) {
            frames_decoded += samples_per_channel;
            double pts = audio_frame->pts * av_q2d(g_ps->format_ctx->streams[g_ps->audio_stream_idx]->time_base);
            update_audio_clock(pts);
        }
        av_frame_free(&audio_frame);
    }
}

// --- libuv Decoder Task ---
void decode_task(uv_work_t* req) {
    PlayerState* ps = (PlayerState*)req->data;
    AVPacket* packet = av_packet_alloc();

    while (av_read_frame(ps->format_ctx, packet) >= 0) {
        if (packet->stream_index == ps->video_stream_idx) {
            if (avcodec_send_packet(ps->video_codec_ctx, packet) == 0) {
                while (1) {
                    AVFrame* frame = av_frame_alloc();
                    int ret = avcodec_receive_frame(ps->video_codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_frame_free(&frame);
                        break;
                    }
                    queue_push(&ps->video_q, frame);
                }
            }
        } else if (packet->stream_index == ps->audio_stream_idx) {
            if (avcodec_send_packet(ps->audio_codec_ctx, packet) == 0) {
                while (1) {
                    AVFrame* frame = av_frame_alloc();
                    int ret = avcodec_receive_frame(ps->audio_codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_frame_free(&frame);
                        break;
                    }
                    queue_push(&ps->audio_q, frame);
                }
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    uv_async_send(&ps->eof_async); // Signal EOF
}

void after_decode_task(uv_work_t* req, int status) {
    // Cleanup if needed
}

void on_eof(uv_async_t* handle) {
    PlayerState* ps = (PlayerState*)handle->data;
    ps->eof = 1;
}

// --- Initialization and Cleanup ---
int init_ffmpeg(PlayerState* ps, const char* filename) {
    ps->format_ctx = NULL;
    if (avformat_open_input(&ps->format_ctx, filename, NULL, NULL) != 0) return -1;
    if (avformat_find_stream_info(ps->format_ctx, NULL) < 0) return -1;

    ps->video_stream_idx = av_find_best_stream(ps->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    ps->audio_stream_idx = av_find_best_stream(ps->format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if (ps->video_stream_idx < 0 || ps->audio_stream_idx < 0) return -1;

    // --- Video Setup ---
    const AVCodec* video_codec = avcodec_find_decoder(ps->format_ctx->streams[ps->video_stream_idx]->codecpar->codec_id);
    ps->video_codec_ctx = avcodec_alloc_context3(video_codec);
    avcodec_parameters_to_context(ps->video_codec_ctx, ps->format_ctx->streams[ps->video_stream_idx]->codecpar);
    avcodec_open2(ps->video_codec_ctx, video_codec, NULL);
    ps->video_time_base = ps->format_ctx->streams[ps->video_stream_idx]->time_base;

    // --- Audio Setup ---
    const AVCodec* audio_codec = avcodec_find_decoder(ps->format_ctx->streams[ps->audio_stream_idx]->codecpar->codec_id);
    ps->audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    avcodec_parameters_to_context(ps->audio_codec_ctx, ps->format_ctx->streams[ps->audio_stream_idx]->codecpar);
    avcodec_open2(ps->audio_codec_ctx, audio_codec, NULL);

    // --- Rescaler and Resampler ---
    ps->sws_ctx = sws_getContext(ps->video_codec_ctx->width, ps->video_codec_ctx->height, ps->video_codec_ctx->pix_fmt,
                                ps->video_codec_ctx->width, ps->video_codec_ctx->height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);

    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&ps->swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_FLT, 44100,
                        &ps->audio_codec_ctx->ch_layout, ps->audio_codec_ctx->sample_fmt, ps->audio_codec_ctx->sample_rate,
                        0, NULL);
    swr_init(ps->swr_ctx);

    return 0;
}

void cleanup(PlayerState* ps) {
    // Wait for decode thread to finish
    uv_run(ps->loop, UV_RUN_ONCE);

    queue_destroy(&ps->video_q);
    queue_destroy(&ps->audio_q);

    sws_freeContext(ps->sws_ctx);
    swr_free(&ps->swr_ctx);
    avcodec_free_context(&ps->video_codec_ctx);
    avcodec_free_context(&ps->audio_codec_ctx);
    avformat_close_input(&ps->format_ctx);

    UnloadTexture(ps->video_texture);
    UnloadImage(ps->video_image);
    DetachAudioStreamProcessor(ps->audio_stream, audio_callback);
    CloseAudioDevice();
    CloseWindow();
}

// --- Main ---
int main(void) {
    g_ps = (PlayerState*)calloc(1, sizeof(PlayerState));
    if (!g_ps) return -1;

    if (init_ffmpeg(g_ps, VIDEO_FILENAME) != 0) {
        printf("Failed to initialize FFmpeg\n");
        free(g_ps);
        return -1;
    }

    InitWindow(g_ps->video_codec_ctx->width, g_ps->video_codec_ctx->height, "Raylib Video Player");
    InitAudioDevice();
    SetTargetFPS(120); // High FPS for smooth polling

    g_ps->video_image = GenImageColor(g_ps->video_codec_ctx->width, g_ps->video_codec_ctx->height, BLACK);
    g_ps->video_texture = LoadTextureFromImage(g_ps->video_image);

    g_ps->audio_stream = LoadAudioStream(44100, 32, 2);
    SetAudioStreamVolume(g_ps->audio_stream, 0.5f);
    AttachAudioStreamProcessor(g_ps->audio_stream, audio_callback);
    PlayAudioStream(g_ps->audio_stream);

    queue_init(&g_ps->video_q);
    queue_init(&g_ps->audio_q);

    g_ps->loop = uv_default_loop();
    g_ps->decode_req.data = g_ps;
    uv_queue_work(g_ps->loop, &g_ps->decode_req, decode_task, after_decode_task);

    g_ps->eof_async.data = g_ps;
    uv_async_init(g_ps->loop, &g_ps->eof_async, on_eof);

    while (!WindowShouldClose() && !g_ps->eof) {
        uv_run(g_ps->loop, UV_RUN_NOWAIT);

        AVFrame* video_frame = queue_peek(&g_ps->video_q);
        if (video_frame) {
            double video_pts = video_frame->pts * av_q2d(g_ps->video_time_base);
            double audio_clock = get_audio_clock();

            if (video_pts <= audio_clock) {
                video_frame = queue_pop(&g_ps->video_q);
                g_ps->last_video_pts = video_pts;

                uint8_t* dst[4] = { g_ps->video_image.data, NULL, NULL, NULL };
                int dst_linesize[4] = { g_ps->video_codec_ctx->width * 4, 0, 0, 0 };
                sws_scale(g_ps->sws_ctx, (uint8_t const* const*)video_frame->data,
                          video_frame->linesize, 0, g_ps->video_codec_ctx->height,
                          dst, dst_linesize);
                UpdateTexture(g_ps->video_texture, g_ps->video_image.data);
                av_frame_free(&video_frame);
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexture(g_ps->video_texture, 0, 0, WHITE);
        DrawText(TextFormat("A/V Diff: %0.3fs", g_ps->audio_clock - g_ps->last_video_pts), 10, 10, 20, LIME);
        DrawFPS(10, 40);
        EndDrawing();
    }

    cleanup(g_ps);
    free(g_ps);
    return 0;
}