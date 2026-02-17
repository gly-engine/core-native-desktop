#include <stdio.h>
#include <string.h>

#define GECND_STREAM_AVLIB_INTERNAL
#define GECND_FFMPEG_LOAD_INTERNAL
#include "gemedia.h"

#ifdef _WIN32
#include <windows.h>
#define LIB_HANDLE HMODULE
#define load_library(path) LoadLibrary(path)
#define get_symbol(lib, sym) GetProcAddress(lib, sym)
#define close_library(lib) FreeLibrary(lib)
#else
#include <dlfcn.h>
#define LIB_HANDLE void*
#define load_library(path) dlopen(path, RTLD_LAZY | RTLD_GLOBAL)
#define get_symbol(lib, sym) dlsym(lib, sym)
#define close_library(lib) dlclose(lib)
#endif

av_api AV;
static bool g_av_loaded = false;

static LIB_HANDLE libavcodec = NULL;
static LIB_HANDLE libavformat = NULL;
static LIB_HANDLE libavutil = NULL;

#define LOAD_SYM(lib, sym) \
    do { \
        *(void**)&AV.sym = get_symbol(lib, #sym); \
        if (!AV.sym) { \
            fprintf(stderr, "dlsym failed for %s\n", #sym); \
            return false; \
        } \
    } while (0)

#define LOAD_SYM_OPT(lib, sym) \
    do { \
        *(void**)&AV.sym = get_symbol(lib, #sym); \
    } while (0)

static void close_libs(void) {
    if (libavcodec) { close_library(libavcodec); libavcodec = NULL; }
    if (libavformat) { close_library(libavformat); libavformat = NULL; }
    if (libavutil) { close_library(libavutil); libavutil = NULL; }
}

typedef struct {
    const char* avcodec;
    const char* avformat;
    const char* avutil;
} FFMpegVersionSet;

static FFMpegVersionSet version_sets[] = {
#ifdef _WIN32
    // FFmpeg 7
    {"avcodec-61.dll", "avformat-61.dll", "avutil-59.dll"},
    // FFmpeg 6
    {"avcodec-60.dll", "avformat-60.dll", "avutil-58.dll"},
    // FFmpeg 5
    {"avcodec-59.dll", "avformat-59.dll", "avutil-57.dll"},
    // FFmpeg 4
    {"avcodec-58.dll", "avformat-58.dll", "avutil-56.dll"},
#else
    // FFmpeg 7
    {"libavcodec.so.61", "libavformat.so.61", "libavutil.so.59"},
    // FFmpeg 6
    {"libavcodec.so.60", "libavformat.so.60", "libavutil.so.58"},
    // FFmpeg 5
    {"libavcodec.so.59", "libavformat.so.59", "libavutil.so.57"},
    // FFmpeg 4
    {"libavcodec.so.58", "libavformat.so.58", "libavutil.so.56"},
    // Unversioned fallback
    {"libavcodec.so", "libavformat.so", "libavutil.so"},
#endif
    {NULL, NULL, NULL, NULL}
};

bool av_load_ffmpeg(void) {
    if (g_av_loaded) return true;
    
    memset(&AV, 0, sizeof(AV));

    bool found_codec = false;
    bool found_format = false;
    bool found_util = false;
    bool found_scale = false;

    for (int i = 0; version_sets[i].avcodec != NULL; i++) {
        libavcodec = load_library(version_sets[i].avcodec);
        libavformat = load_library(version_sets[i].avformat);
        libavutil = load_library(version_sets[i].avutil);

        if (libavcodec) found_codec = true;
        if (libavformat) found_format = true;
        if (libavutil) found_util = true;

        if (libavcodec && libavformat && libavutil) {
            fprintf(stderr, "[ffmpeg] Successfully loaded FFmpeg libraries:\n");
            fprintf(stderr, "  avcodec:  %s\n", version_sets[i].avcodec);
            fprintf(stderr, "  avformat: %s\n", version_sets[i].avformat);
            fprintf(stderr, "  avutil:   %s\n", version_sets[i].avutil);

            // libavcodec
            LOAD_SYM(libavcodec, avcodec_find_decoder);
            LOAD_SYM(libavcodec, avcodec_find_decoder_by_name);
            LOAD_SYM(libavcodec, avcodec_alloc_context3);
            LOAD_SYM(libavcodec, avcodec_parameters_to_context);
            LOAD_SYM(libavcodec, avcodec_open2);
            LOAD_SYM(libavcodec, avcodec_send_packet);
            LOAD_SYM(libavcodec, avcodec_receive_frame);
            LOAD_SYM(libavcodec, avcodec_free_context);
            LOAD_SYM(libavcodec, avcodec_flush_buffers);
            LOAD_SYM(libavcodec, av_packet_alloc);
            LOAD_SYM(libavcodec, av_packet_unref);
            LOAD_SYM(libavcodec, av_packet_free);
            LOAD_SYM(libavcodec, av_frame_alloc);
            LOAD_SYM(libavcodec, av_frame_free);
            LOAD_SYM(libavcodec, av_frame_unref);
            LOAD_SYM(libavcodec, av_codec_iterate);
            LOAD_SYM(libavcodec, avcodec_get_name);
            LOAD_SYM(libavcodec, av_codec_is_encoder);
            LOAD_SYM(libavcodec, av_codec_is_decoder);

            // libavformat
            LOAD_SYM(libavformat, avformat_open_input);
            LOAD_SYM(libavformat, avformat_find_stream_info);
            LOAD_SYM(libavformat, av_dump_format);
            LOAD_SYM(libavformat, avformat_close_input);
            LOAD_SYM(libavformat, av_find_best_stream);
            LOAD_SYM(libavformat, av_read_frame);
            LOAD_SYM(libavformat, av_seek_frame);
            LOAD_SYM(libavformat, avformat_network_init);
            LOAD_SYM(libavformat, avformat_network_deinit);
            LOAD_SYM(libavformat, av_find_input_format);
        
            // libavutil
            LOAD_SYM(libavutil, av_gettime_relative);
            LOAD_SYM(libavutil, av_strerror);
            LOAD_SYM(libavutil, av_log_set_level);
            LOAD_SYM(libavutil, av_dict_set);
            LOAD_SYM(libavutil, av_dict_free);
            LOAD_SYM(libavutil, av_image_get_buffer_size);
            LOAD_SYM(libavutil, av_image_fill_arrays);

            // Optional symbols
            LOAD_SYM_OPT(libavutil, av_hwframe_transfer_data);
            
            g_av_loaded = true;
            return true;
        }
        close_libs();
    }

    fprintf(stderr, "[ffmpeg] Failed to load a compatible set of FFmpeg libraries.\n");
    fprintf(stderr, "  avcodec: %s\n", found_codec ? "found, but not in a compatible set" : "not found");
    fprintf(stderr, "  avformat: %s\n", found_format ? "found, but not in a compatible set" : "not found");
    fprintf(stderr, "  avutil: %s\n", found_util ? "found, but not in a compatible set" : "not found");
    return false;
}
