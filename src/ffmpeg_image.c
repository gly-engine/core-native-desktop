#include "zeebo.h"
#include "SDL2/SDL.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/dict.h"

typedef struct TextureCache {
    AVDictionary *dict;
} TextureCache;

TextureCache texture_cache = {NULL};
extern SDL_Renderer *renderer;

static SDL_Texture *load_image(const char *file_path)
{
    static char texture_address[32];
    SDL_Surface *surface = NULL;
    SDL_Texture *texture = NULL;
    AVFormatContext *format_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *frame = NULL;
    int video_stream = -1;
    int ret;

    do {
        AVDictionaryEntry *entry = av_dict_get(texture_cache.dict, file_path, NULL, 0);
        if (entry) {
            texture = (SDL_Texture *) strtoul(entry->value, NULL, 16);
            break;
        }

        if (avformat_open_input(&format_ctx, file_path, NULL, NULL) != 0) {
            kernel_add_error("file not found");
            break;
        }

        if (avformat_find_stream_info(format_ctx, NULL) < 0) {
            break;
        }

        for (int i = 0; i < format_ctx->nb_streams; i++) {
            if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream = i;
                break;
            }
        }

        if (video_stream == -1) {
            break;
        }

        const AVCodec *codec = avcodec_find_decoder(format_ctx->streams[video_stream]->codecpar->codec_id);
        if (!codec) {
            break;
        }

        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) {
            break;
        }

        if (avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream]->codecpar) < 0) {
            break;
        }

        if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
            break;
        }

        frame = av_frame_alloc();
        if (!frame) {
            break;
        }

        AVPacket packet;
        memset(&packet, 0, sizeof(packet));
        while (av_read_frame(format_ctx, &packet) >= 0) {
            if (packet.stream_index == video_stream) {
                ret = avcodec_send_packet(codec_ctx, &packet);
                if (ret < 0) {
                    av_packet_unref(&packet);
                    break;
                }
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret >= 0) {
                    av_packet_unref(&packet);
                    break;
                }
            }
            av_packet_unref(&packet);
        }

        surface = SDL_CreateRGBSurfaceFrom(
            frame->data[0], codec_ctx->width, codec_ctx->height, 32, frame->linesize[0],
            0x0000FF, 0x00FF00, 0xFF0000, 0
        );

        if (!surface) {
            break;
        }

        texture = SDL_CreateTextureFromSurface(renderer, surface);
        snprintf(texture_address, sizeof(texture_address), "%p", texture);
        av_dict_set(&texture_cache.dict, file_path, texture_address, 0);
    }
    while(0);
    
    if (frame) {
        av_frame_free(&frame);
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }
    if (format_ctx) {
        avformat_close_input(&format_ctx);
    }
    if (surface) {
        SDL_FreeSurface(surface);
    }

    return texture;
}

void ffmpeg_image_draw(double x, double y, const char *file_path) {
    uint8_t r, g, b, a;

    SDL_Texture *texture = load_image(file_path);
    if (!texture) {
        kernel_add_error("failed to draw image");
        kernel_add_error(file_path);
        return;
    }

    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_Rect rect = {x, y, 0, 0};
    SDL_QueryTexture(texture, NULL, NULL, &rect.w, &rect.h);
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
}
