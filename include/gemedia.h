#ifndef GEMEDIA_H
#define GEMEDIA_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#if defined(GECND_STREAM_AVLIB_INTERNAL)

#include <uv.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libavutil/samplefmt.h>

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext  *vcodec;
    AVStream        *video;
    int video_index;
    struct SwsContext *sws;

    AVCodecContext  *acodec;
    AVStream        *audio;
    int audio_index;
    int16_t *audio_buf;
    int audio_buf_samples;

    /* audio output: ring buffer of stereo S16 frames + dedicated drain thread */
    uv_thread_t athread;
    atomic_int  athread_running;
    int16_t    *aring;          /* 2 * aring_cap int16_t */
    size_t      aring_cap;      /* capacity in stereo frames */
    size_t      aring_head;
    size_t      aring_tail;
    uv_mutex_t  aring_mtx;
    uv_cond_t   aring_cv;
    int         athread_ready;

    uv_thread_t thread;
    atomic_int running;
    atomic_int paused;
    atomic_int state;        /* gdmsp_fsm_t — lock-free snapshot */
    double clock_start;
    int64_t start_pts;
    char *url;
} VideoStream;

MediaFrame* stream_get_frame(VideoStream *stream);

#endif // GECND_STREAM_AVLIB_INTERNAL


#if defined(GECND_FFMPEG_LOAD_INTERNAL)

#include <libavutil/opt.h>
#include <libavutil/rational.h>
#include <libavutil/error.h>
#include <libavutil/dict.h>
#include <libavutil/log.h>
#include <libavutil/imgutils.h>

typedef struct {
    // libavcodec
    const AVCodec* (*avcodec_find_decoder)(enum AVCodecID id);
    const AVCodec* (*avcodec_find_decoder_by_name)(const char *name);
    AVCodecContext* (*avcodec_alloc_context3)(const AVCodec *codec);
    int (*avcodec_parameters_to_context)(AVCodecContext *context, const AVCodecParameters *par);
    int (*avcodec_open2)(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options);
    int (*avcodec_send_packet)(AVCodecContext *avctx, const AVPacket *avpkt);
    int (*avcodec_receive_frame)(AVCodecContext *avctx, AVFrame *frame);
    void (*avcodec_free_context)(AVCodecContext **avctx);
    void (*avcodec_flush_buffers)(AVCodecContext *avctx);
    AVPacket* (*av_packet_alloc)(void);
    void (*av_packet_unref)(AVPacket *pkt);
    void (*av_packet_free)(AVPacket **pkt);
    AVFrame* (*av_frame_alloc)(void);
    void (*av_frame_free)(AVFrame **frame);
    void (*av_frame_unref)(AVFrame *frame);
    int (*av_hwframe_transfer_data)(AVFrame *dst, const AVFrame *src, int flags);
    const AVCodec* (*av_codec_iterate)(void **opaque);
    const char* (*avcodec_get_name)(enum AVCodecID id);
    int (*av_codec_is_encoder)(const AVCodec *codec);
    int (*av_codec_is_decoder)(const AVCodec *codec);

    const AVCodec* (*avcodec_find_encoder)(enum AVCodecID id);
    int (*avcodec_send_frame)(AVCodecContext *avctx, const AVFrame *frame);
    int (*avcodec_receive_packet)(AVCodecContext *avctx, AVPacket *avpkt);

    // libavformat
    AVFormatContext* (*avformat_alloc_context)(void);
    int (*avformat_open_input)(AVFormatContext **ps, const char *url, const AVInputFormat *fmt, AVDictionary **options);
    int (*avformat_find_stream_info)(AVFormatContext *ic, AVDictionary **options);
    void (*av_dump_format)(AVFormatContext *ic, int index, const char *url, int is_output);
    void (*avformat_close_input)(AVFormatContext **s);
    int (*av_find_best_stream)(AVFormatContext *ic, enum AVMediaType type, int wanted_stream_nb, int related_stream, const AVCodec **decoder_ret, int flags);
    int (*av_read_frame)(AVFormatContext *s, AVPacket *pkt);
    int (*av_seek_frame)(AVFormatContext *s, int stream_index, int64_t timestamp, int flags);
    void (*avformat_network_init)(void);
    void (*avformat_network_deinit)(void);
    AVInputFormat* (*av_find_input_format)(const char *short_name);
    int (*avformat_alloc_output_context2)(AVFormatContext **ctx, const AVOutputFormat *oformat, const char *format_name, const char *filename);
    AVStream* (*avformat_new_stream)(AVFormatContext *s, const AVCodec *c);
    int (*avformat_write_header)(AVFormatContext *s, AVDictionary **options);
    int (*av_interleaved_write_frame)(AVFormatContext *s, AVPacket *pkt);
    int (*av_write_trailer)(AVFormatContext *s);
    void (*avformat_free_context)(AVFormatContext *s);
    int (*avio_open_dyn_buf)(AVIOContext **s);
    int (*avio_close_dyn_buf)(AVIOContext *s, uint8_t **pbuffer);
    AVIOContext* (*avio_alloc_context)(unsigned char *buffer, int buffer_size, int write_flag, void *opaque, int (*read_packet)(void*, uint8_t*, int), int (*write_packet)(void*, uint8_t*, int), int64_t (*seek)(void*, int64_t, int));
    void (*avio_flush)(AVIOContext *s);

    // libswscale
    struct SwsContext* (*sws_getContext)(int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat, int flags, SwsFilter *srcFilter, SwsFilter *dstFilter, const double *param);
    int (*sws_scale)(struct SwsContext *c, const uint8_t *const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH, uint8_t *const dst[], const int dstStride[]);
    void (*sws_freeContext)(struct SwsContext *swsContext);

    int (*avcodec_parameters_from_context)(AVCodecParameters *par, const AVCodecContext *codec);

    // libavutil
    void*  (*av_malloc)(size_t size);
    void   (*av_free)(void *ptr);
    int64_t (*av_gettime_relative)(void);
    int (*av_strerror)(int errnum, char *errbuf, size_t errbuf_size);
    void (*av_log_set_level)(int level);
    int (*av_dict_set)(AVDictionary **pm, const char *key, const char *value, int flags);
    void (*av_dict_free)(AVDictionary **m);
    int (*av_image_get_buffer_size)(enum AVPixelFormat pix_fmt, int width, int height, int align);
    int (*av_image_fill_arrays)(uint8_t *dst_data[4], int dst_linesize[4], const uint8_t *src, enum AVPixelFormat pix_fmt, int width, int height, int align);

    int (*av_opt_set)(void *obj, const char *name, const char *val, int search_flags);
    int (*av_opt_set_int)(void *obj, const char *name, int64_t val, int search_flags);
    int (*av_opt_set_q)(void *obj, const char *name, AVRational val, int search_flags);
} av_api;

extern av_api AV;

bool av_load_ffmpeg(void);

static inline double gly_av_q2d(AVRational a) {
    return (double)a.num / (double)a.den;
}

#endif

#endif
