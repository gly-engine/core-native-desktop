#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gecnd.h"
#include "gdmsp.h"

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

/* FFmpeg chama esse callback periodicamente durante I/O blocking.
 * Retornar !=0 aborta a operação — sem isso, ctrl+c durante
 * avformat_open_input/av_read_frame pode travar o main thread por segundos. */
static int interrupt_cb(void *opaque) {
    VideoStream *s = opaque;
    return atomic_load(&s->running) ? 0 : 1;
}

static void threadworker(void *arg) {
    VideoStream *s = arg;
    AVPacket *pkt   = NULL;
    AVFrame  *vfrm  = NULL;
    bool initialized = false;
    double pause_at  = 0.0;

    AV.avformat_network_init();

    s->fmt = AV.avformat_alloc_context();
    if (!s->fmt) {
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_done;
    }
    s->fmt->interrupt_callback.callback = interrupt_cb;
    s->fmt->interrupt_callback.opaque   = s;

    AVDictionary *opts = NULL;
    AV.av_dict_set(&opts, "timeout", "5000000", 0);
    AV.av_dict_set(&opts, "tls_verify", "0", 0);

    if (AV.avformat_open_input(&s->fmt, s->url, NULL, &opts) < 0) {
        /* avformat_open_input libera o context em caso de falha */
        s->fmt = NULL;
        fprintf(stderr, "[media] error opening: %s\n", s->url);
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        AV.av_dict_free(&opts);
        goto cleanup_done;
    }
    AV.av_dict_free(&opts);

    if (!atomic_load(&s->running)) goto cleanup_format;

    s->fmt->max_analyze_duration = 0;
    if (AV.avformat_find_stream_info(s->fmt, NULL) < 0) {
        fprintf(stderr, "[media] failed to find stream info: %s\n", s->url);
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_format;
    }

    if (s->fmt->duration != AV_NOPTS_VALUE && s->fmt->duration > 0)
        atomic_store(&s->dur_ms, s->fmt->duration / (AV_TIME_BASE / 1000));

    s->video_index = AV.av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (s->video_index < 0) {
        fprintf(stderr, "[media] no video stream\n");
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_format;
    }

    s->video = s->fmt->streams[s->video_index];
    const AVCodec *vdec = AV.avcodec_find_decoder(s->video->codecpar->codec_id);
    if (!vdec) {
        fprintf(stderr, "[media] no decoder for codec %d\n", s->video->codecpar->codec_id);
        atomic_store(&s->state, GDMSP_FSM_ERROR);
        goto cleanup_format;
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
    s->clock_start = now_sec();

    AV.av_seek_frame(s->fmt, s->video_index, 0, AVSEEK_FLAG_BACKWARD);
    AV.avcodec_flush_buffers(s->vcodec);

    while (atomic_load(&s->running)) {
        /* pause real: bloqueia avanço e empurra clock pra frente ao retornar */
        if (atomic_load(&s->paused)) {
            if (pause_at == 0.0) {
                pause_at = now_sec();
                atomic_store(&s->state, GDMSP_FSM_PAUSED);
            }
            usleep(10000);
            continue;
        } else if (pause_at > 0.0) {
            s->clock_start += now_sec() - pause_at;
            pause_at = 0.0;
            atomic_store(&s->state, GDMSP_FSM_PLAYING);
        }

        if (AV.av_read_frame(s->fmt, pkt) < 0) {
            if (!atomic_load(&s->running)) break;
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
                    atomic_store(&s->cur_ms, (long long)(pts * 1000.0));

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

    if (vfrm) AV.av_frame_free(&vfrm);
    if (pkt)  AV.av_packet_free(&pkt);
cleanup_codec:
    if (s->vcodec) AV.avcodec_free_context(&s->vcodec);
cleanup_format:
    if (s->fmt) AV.avformat_close_input(&s->fmt);
cleanup_done:
    AV.avformat_network_deinit();
    /* sinal final pro reaper externo (av_state) — qualquer paths-de-erro
     * acima já marcou ERROR, mas IDLE atômico aqui é o gatilho do join. */
    atomic_store(&s->state, GDMSP_FSM_IDLE);
}

static VideoStream *stream_create(const char *url) {
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

/* ── player callbacks ─────────────────────────────────────────────── */

static VideoStream *s_streams[4] = {0};

static void av_reap(uint8_t channel) {
    VideoStream *s = s_streams[channel];
    if (!s) return;
    uv_thread_join(&s->thread);
    free(s->url);
    free(s);
    s_streams[channel] = NULL;
    gamely_daemon_media_background_release();
}

static gdmsp_fsm_t av_source(uint8_t channel, const char *url, void *usr) {
    (void)usr;
    if (channel >= 4) return GDMSP_FSM_ERROR;

    /* canal ocupado: se thread já saiu, faz reap; senão sinaliza stop e
     * retorna ERROR — o service não deve chamar source() nesse estado. */
    if (s_streams[channel]) {
        gdmsp_fsm_t st = (gdmsp_fsm_t) atomic_load(&s_streams[channel]->state);
        if (st == GDMSP_FSM_IDLE) {
            av_reap(channel);
        } else {
            atomic_store(&s_streams[channel]->running, 0);
            return GDMSP_FSM_ERROR;
        }
    }

    if (!gamely_daemon_media_background_claim()) {
        fprintf(stderr, "[media] background buffer in use\n");
        return GDMSP_FSM_ERROR;
    }
    s_streams[channel] = stream_create(url);
    if (!s_streams[channel]) {
        gamely_daemon_media_background_release();
        return GDMSP_FSM_ERROR;
    }
    return GDMSP_FSM_LOADING;
}

static gdmsp_fsm_t av_set(uint8_t channel, gdmsp_cmd_t cmd, gdmsp_value_t value, void *usr) {
    (void)usr; (void)value;
    if (channel >= 4 || !s_streams[channel]) return GDMSP_FSM_IDLE;
    VideoStream *s = s_streams[channel];

    switch (cmd) {
        case GDMSP_CMD_RESOURCE:
        case GDMSP_CMD_STOP:
            atomic_store(&s->running, 0);
            atomic_store(&s->paused, 0);  /* destrava worker preso em pause loop */
            {
                gdmsp_fsm_t cur = (gdmsp_fsm_t) atomic_load(&s->state);
                if (cur != GDMSP_FSM_ERROR && cur != GDMSP_FSM_IDLE)
                    atomic_store(&s->state, GDMSP_FSM_STOPPING);
            }
            break;

        case GDMSP_CMD_PLAY:
            atomic_store(&s->paused, 0);
            break;

        case GDMSP_CMD_PAUSE:
            atomic_store(&s->paused, 1);
            break;
    
        case GDMSP_CMD_TICK:
            break;

        case GDMSP_CMD_CURRENT_TIME:
            /** @todo seek */
            break;

        case GDMSP_CMD_POSITION:
            gecnd_filter_set_video_pos(value.x, value.y, value.w, value.h);
            break;

        default:
            break;
    }

    gdmsp_fsm_t st = (gdmsp_fsm_t) atomic_load(&s->state);
    if (st == GDMSP_FSM_IDLE) {
        av_reap(channel);
        return GDMSP_FSM_IDLE;
    }

    /** @todo fix state — o decoder abre de forma assíncrona e o LOADING→PLAYING
     *  real depende do primeiro frame decodificado (que hoje não sai: o
     *  receive_frame nunca retorna 0). Reportar PLAYING no TICK evita o status
     *  preso em 'loading'; remover quando o pipeline de decode estiver ok. */
    if (cmd == GDMSP_CMD_TICK && st == GDMSP_FSM_LOADING)
        return GDMSP_FSM_PLAYING;

    return st;
}

static gdmsp_value_t av_get(uint8_t channel, gdmsp_cmd_t cmd, void *usr) {
    (void)usr;

    gdmsp_value_t value = { -1 };
    
    if (channel >= 4 || !s_streams[channel]) return value;
    VideoStream *s = s_streams[channel];

    switch (cmd) {
        case GDMSP_CMD_CURRENT_TIME:
            value.i64 = atomic_load(&s->cur_ms);
            break;
        
        case GDMSP_CMD_DURATION:
            value.i64 = atomic_load(&s->dur_ms);
    }

    return value;
}

static gdmsp_player_t media_player = {
    .src = av_source,
    .set = av_set,
    .get = av_get,
};

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "media_player:$s", &media_player, NULL);
    gecnd_registry("set", "media_player:ffmpeg+$0", &media_player, NULL);
}