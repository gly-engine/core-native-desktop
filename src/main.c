// SPDX-License-Identifier: Unlicense

#include <raylib.h>
#include <uv.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_QUEUE_SIZE 30
#define VIDEO_FILENAME "/home/rodrigao/Videos/rick-h264.mp4"

typedef struct FrameQueue {
    AVFrame* frames[MAX_QUEUE_SIZE];
    int size;
    int head;
    int tail;
    uv_mutex_t mutex;
    uv_cond_t cond;
} FrameQueue;

void queue_init(FrameQueue* q) {
    memset(q, 0, sizeof(FrameQueue));
    uv_mutex_init(&q->mutex);
    uv_cond_init(&q->cond);
}

void queue_push(FrameQueue* q, AVFrame* frame) {
    uv_mutex_lock(&q->mutex);
    while (q->size >= MAX_QUEUE_SIZE) {
        uv_cond_wait(&q->cond, &q->mutex);
    }
    q->frames[q->head] = frame;
    q->head = (q->head + 1) % MAX_QUEUE_SIZE;
    q->size++;
    uv_cond_signal(&q->cond);
    uv_mutex_unlock(&q->mutex);
}

AVFrame* queue_pop(FrameQueue* q) {
    uv_mutex_lock(&q->mutex);
    while (q->size <= 0) {
        uv_cond_wait(&q->cond, &q->mutex);
    }
    AVFrame* frame = q->frames[q->tail];
    q->tail = (q->tail + 1) % MAX_QUEUE_SIZE;
    q->size--;
    uv_cond_signal(&q->cond);
    uv_mutex_unlock(&q->mutex);
    return frame;
}

AVFrame* queue_peek(FrameQueue* q) {
    uv_mutex_lock(&q->mutex);
    AVFrame* frame = NULL;
    if (q->size > 0) {
        frame = q->frames[q->tail];
    }
    uv_mutex_unlock(&q->mutex);
    return frame;
}

void queue_destroy(FrameQueue* q) {
    while(q->size > 0) {
        AVFrame* frame = queue_pop(q);
        av_frame_free(&frame);
    }
    uv_mutex_destroy(&q->mutex);
    uv_cond_destroy(&q->cond);
}

typedef struct PlayerState {
    AVFormatContext* format_ctx;
    AVCodecContext* video_codec_ctx;
    AVCodecContext* audio_codec_ctx;
    int video_stream_idx;
    int audio_stream_idx;
    struct SwsContext* sws_ctx;
    struct SwrContext* swr_ctx;
    AVRational video_time_base;

    FrameQueue video_q;
    FrameQueue audio_q;

    uv_loop_t* loop;
    uv_work_t decode_req;
    uv_async_t eof_async;
    uv_timer_t audio_timer;
    uv_timer_t video_timer;
    uv_timer_t main_timer;
    int eof;

    Texture2D video_texture;
    AudioStream audio_stream;
    Image video_image;

    double start_time;
    double audio_clock;
    double last_video_pts;

} PlayerState;

PlayerState* g_ps = NULL;

void update_audio_clock(double pts) {
    if (g_ps) g_ps->audio_clock = pts;
}

double get_audio_clock() {
    return g_ps ? g_ps->audio_clock : 0.0;
}

void main_timer_cb(uv_timer_t* handle);

void video_timer_cb(uv_timer_t* handle) {
    PlayerState* ps = (PlayerState*)handle->data;

    double video_clock = GetTime() - ps->start_time;
    AVFrame* video_frame = queue_peek(&ps->video_q);

    if (video_frame) {
        double video_pts = video_frame->pts * av_q2d(ps->video_time_base);

        if (video_pts <= video_clock) {
            video_frame = queue_pop(&ps->video_q);
            ps->last_video_pts = video_pts;

            uint8_t* dst[4] = { ps->video_image.data, NULL, NULL, NULL };
            int dst_linesize[4] = { ps->video_codec_ctx->width * 4, 0, 0, 0 };
            sws_scale(ps->sws_ctx, (uint8_t const* const*)video_frame->data,
                      video_frame->linesize, 0, ps->video_codec_ctx->height,
                      dst, dst_linesize);
            UpdateTexture(ps->video_texture, ps->video_image.data);
            av_frame_free(&video_frame);

            uv_timer_start(&ps->video_timer, video_timer_cb, 0, 0);
        } else {
            double delay = video_pts - video_clock;
            uv_timer_start(&ps->video_timer, video_timer_cb, (uint64_t)(delay * 1000), 0);
        }
    } else {
        uv_timer_start(&ps->video_timer, video_timer_cb, 10, 0);
    }
}

void main_timer_cb(uv_timer_t* handle) {
    PlayerState* ps = (PlayerState*)handle->data;

    if (WindowShouldClose() || ps->eof) {
        uv_stop(ps->loop);
        return;
    }

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexture(ps->video_texture, 0, 0, WHITE);
    DrawText(TextFormat("A/V Diff: %0.3fs", ps->audio_clock - ps->last_video_pts), 10, 10, 20, LIME);
    DrawFPS(10, 40);
    EndDrawing();
}

void audio_timer_cb(uv_timer_t* handle) {
    PlayerState* ps = (PlayerState*)handle->data;
    if (!ps->swr_ctx) {
        uv_timer_start(&ps->audio_timer, audio_timer_cb, 10, 0);
        return;
    }

    if (IsAudioStreamProcessed(ps->audio_stream)) {
        float* buffer = (float*)calloc(ps->audio_codec_ctx->frame_size, ps->audio_codec_ctx->ch_layout.nb_channels * sizeof(float));
        if (!buffer) {
            uv_timer_start(&ps->audio_timer, audio_timer_cb, 10, 0);
            return;
        }

        int samples_to_fill = ps->audio_codec_ctx->frame_size * ps->audio_codec_ctx->ch_layout.nb_channels;
        int buffer_offset = 0;

        while (samples_to_fill > 0) {
            AVFrame* audio_frame = queue_pop(&ps->audio_q);
            if (!audio_frame) {
                memset(buffer + buffer_offset, 0, samples_to_fill * sizeof(float));
                break;
            }

            uint8_t* resampled_data = NULL;
            int max_out_samples = audio_frame->nb_samples * 2;
            av_samples_alloc(&resampled_data, NULL, ps->audio_codec_ctx->ch_layout.nb_channels, max_out_samples, AV_SAMPLE_FMT_FLT, 0);

            int converted_samples = swr_convert(ps->swr_ctx,
                                                &resampled_data,
                                                max_out_samples,
                                                (const uint8_t**)audio_frame->data,
                                                audio_frame->nb_samples);

            if (converted_samples > 0) {
                int samples_to_copy = converted_samples * ps->audio_codec_ctx->ch_layout.nb_channels;
                if (samples_to_copy > samples_to_fill) {
                    samples_to_copy = samples_to_fill;
                }
                memcpy(buffer + buffer_offset, resampled_data, samples_to_copy * sizeof(float));
                buffer_offset += samples_to_copy;
                samples_to_fill -= samples_to_copy;
            }

            av_freep(&resampled_data);
            double pts = audio_frame->pts * av_q2d(ps->format_ctx->streams[ps->audio_stream_idx]->time_base);
            update_audio_clock(pts);
            av_frame_free(&audio_frame);
        }

        UpdateAudioStream(ps->audio_stream, buffer, ps->audio_codec_ctx->frame_size);
        free(buffer);
    }
    uv_timer_start(&ps->audio_timer, audio_timer_cb, 0, 0);
}

void audio_timer_cb(uv_timer_t* handle);
void video_timer_cb(uv_timer_t* handle);

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
    uv_async_send(&ps->eof_async);
}

void after_decode_task(uv_work_t* req, int status) {}

void on_eof(uv_async_t* handle) {
    PlayerState* ps = (PlayerState*)handle->data;
    ps->eof = 1;
}

int init_ffmpeg(PlayerState* ps, const char* filename) {
    ps->format_ctx = NULL;
    if (avformat_open_input(&ps->format_ctx, filename, NULL, NULL) != 0) return -1;
    if (avformat_find_stream_info(ps->format_ctx, NULL) < 0) return -1;

    ps->video_stream_idx = av_find_best_stream(ps->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    ps->audio_stream_idx = av_find_best_stream(ps->format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if (ps->video_stream_idx < 0 || ps->audio_stream_idx < 0) return -1;

    const AVCodec* video_codec = avcodec_find_decoder(ps->format_ctx->streams[ps->video_stream_idx]->codecpar->codec_id);
    ps->video_codec_ctx = avcodec_alloc_context3(video_codec);
    avcodec_parameters_to_context(ps->video_codec_ctx, ps->format_ctx->streams[ps->video_stream_idx]->codecpar);
    avcodec_open2(ps->video_codec_ctx, video_codec, NULL);
    ps->video_time_base = ps->format_ctx->streams[ps->video_stream_idx]->time_base;

    const AVCodec* audio_codec = avcodec_find_decoder(ps->format_ctx->streams[ps->audio_stream_idx]->codecpar->codec_id);
    ps->audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    avcodec_parameters_to_context(ps->audio_codec_ctx, ps->format_ctx->streams[ps->audio_stream_idx]->codecpar);
    avcodec_open2(ps->audio_codec_ctx, audio_codec, NULL);

    ps->sws_ctx = sws_getContext(ps->video_codec_ctx->width, ps->video_codec_ctx->height, ps->video_codec_ctx->pix_fmt,
                                ps->video_codec_ctx->width, ps->video_codec_ctx->height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);

    swr_alloc_set_opts2(&ps->swr_ctx, &ps->audio_codec_ctx->ch_layout, AV_SAMPLE_FMT_FLT, ps->audio_codec_ctx->sample_rate,
                        &ps->audio_codec_ctx->ch_layout, ps->audio_codec_ctx->sample_fmt, ps->audio_codec_ctx->sample_rate,
                        0, NULL);
    swr_init(ps->swr_ctx);

    return 0;
}

void cleanup(PlayerState* ps) {
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
    CloseAudioDevice();
    CloseWindow();
}

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
    SetTargetFPS(0);

    g_ps->video_image = GenImageColor(g_ps->video_codec_ctx->width, g_ps->video_codec_ctx->height, BLACK);
    g_ps->video_texture = LoadTextureFromImage(g_ps->video_image);

    g_ps->audio_stream = LoadAudioStream(g_ps->audio_codec_ctx->sample_rate, 32, g_ps->audio_codec_ctx->ch_layout.nb_channels);
    SetAudioStreamVolume(g_ps->audio_stream, 0.5f);
    PlayAudioStream(g_ps->audio_stream);

    queue_init(&g_ps->video_q);
    queue_init(&g_ps->audio_q);

    g_ps->loop = uv_default_loop();
    g_ps->decode_req.data = g_ps;
    uv_queue_work(g_ps->loop, &g_ps->decode_req, decode_task, after_decode_task);

    g_ps->eof_async.data = g_ps;
    uv_async_init(g_ps->loop, &g_ps->eof_async, on_eof);

    uv_timer_init(g_ps->loop, &g_ps->video_timer);
    g_ps->video_timer.data = g_ps;
    uv_timer_start(&g_ps->video_timer, video_timer_cb, 0, 0);

    uv_timer_init(g_ps->loop, &g_ps->audio_timer);
    g_ps->audio_timer.data = g_ps;
    uv_timer_start(&g_ps->audio_timer, audio_timer_cb, 0, 0);

    uv_timer_init(g_ps->loop, &g_ps->main_timer);
    g_ps->main_timer.data = g_ps;
    uv_timer_start(&g_ps->main_timer, main_timer_cb, 0, 16);

    g_ps->start_time = GetTime();

    uv_run(g_ps->loop, UV_RUN_DEFAULT);

    cleanup(g_ps);
    free(g_ps);
    return 0;
}
