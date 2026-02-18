#ifndef GEMEDIA_H
#define GEMEDIA_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct {
    uint8_t *data[4];
    int linesize[4];
    int width;
    int height;
    int format;
    double pts;
    atomic_bool ready;
} MediaFrame;

MediaFrame* avlib_get_background_frame(void);

#if defined(GECND_STREAM_AVLIB_INTERNAL)

#include <uv.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext  *vcodec;
    AVStream        *video;
    int video_index;
    struct SwsContext *sws;

    MediaFrame frames[2];
    atomic_int front;

    uv_thread_t thread;
    atomic_int running;
    atomic_int paused;
    double clock_start;
    int64_t start_pts;
    char *url;
} VideoStream;

VideoStream* stream_create(const char *url);
void stream_destroy(VideoStream *stream);
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

    // libavformat
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

    // libswscale
    struct SwsContext* (*sws_getContext)(int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat, int flags, SwsFilter *srcFilter, SwsFilter *dstFilter, const double *param);
    int (*sws_scale)(struct SwsContext *c, const uint8_t *const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH, uint8_t *const dst[], const int dstStride[]);
    void (*sws_freeContext)(struct SwsContext *swsContext);

    // libavutil
    int64_t (*av_gettime_relative)(void);
    int (*av_strerror)(int errnum, char *errbuf, size_t errbuf_size);
    void (*av_log_set_level)(int level);
    int (*av_dict_set)(AVDictionary **pm, const char *key, const char *value, int flags);
    void (*av_dict_free)(AVDictionary **m);
    int (*av_image_get_buffer_size)(enum AVPixelFormat pix_fmt, int width, int height, int align);
    int (*av_image_fill_arrays)(uint8_t *dst_data[4], int dst_linesize[4], const uint8_t *src, enum AVPixelFormat pix_fmt, int width, int height, int align);
} av_api;

extern av_api AV;

bool av_load_ffmpeg(void);

static inline double gly_av_q2d(AVRational a) {
    return (double)a.num / (double)a.den;
}

#endif

#endif
