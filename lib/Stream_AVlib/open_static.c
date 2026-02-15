#include <stdbool.h>

#define GECND_STREAM_AVLIB_INTERNAL
#define GECND_FFMPEG_LOAD_INTERNAL
#include "gemedia.h"

av_api AV;

bool av_load_ffmpeg(void) {
    // libavcodec
    AV.avcodec_find_decoder = avcodec_find_decoder;
    AV.avcodec_alloc_context3 = avcodec_alloc_context3;
    AV.avcodec_parameters_to_context = avcodec_parameters_to_context;
    AV.avcodec_open2 = avcodec_open2;
    AV.avcodec_send_packet = avcodec_send_packet;
    AV.avcodec_receive_frame = avcodec_receive_frame;
    AV.avcodec_free_context = avcodec_free_context;
    AV.avcodec_flush_buffers = avcodec_flush_buffers;
    AV.av_packet_alloc = av_packet_alloc;
    AV.av_packet_unref = av_packet_unref;
    AV.av_packet_free = av_packet_free;
    AV.av_frame_alloc = av_frame_alloc;
    AV.av_frame_free = av_frame_free;
    AV.av_frame_unref = av_frame_unref;
    AV.av_hwframe_transfer_data = av_hwframe_transfer_data;

    // libavformat
    AV.avformat_open_input = avformat_open_input;
    AV.avformat_find_stream_info = avformat_find_stream_info;
    AV.avformat_close_input = avformat_close_input;
    AV.av_find_best_stream = av_find_best_stream;
    AV.av_read_frame = av_read_frame;
    AV.av_seek_frame = av_seek_frame;
    AV.avformat_network_init = avformat_network_init;
    AV.avformat_network_deinit = avformat_network_deinit;

    // libswscale
    AV.sws_getContext = sws_getContext;
    AV.sws_scale = sws_scale;
    AV.sws_freeContext = sws_freeContext;

    // libavutil
    AV.av_gettime_relative = av_gettime_relative;
    AV.av_strerror = av_strerror;
    AV.av_log_set_level = av_log_set_level;
    AV.av_dict_set = av_dict_set;
    AV.av_dict_free = av_dict_free;
    AV.av_image_get_buffer_size = av_image_get_buffer_size;
    AV.av_image_fill_arrays = av_image_fill_arrays;

    return true;
}
