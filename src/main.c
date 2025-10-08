#include "raylib.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Video Player");

    // FFmpeg variables
    AVFormatContext *pFormatCtx = NULL;
    int             i, videoStream;
    AVCodecContext  *pCodecCtx = NULL;
    AVCodec         *pCodec = NULL;
    AVFrame         *pFrame = NULL;
    AVPacket        packet;
    struct SwsContext *sws_ctx = NULL;

    // Open video file
    if (avformat_open_input(&pFormatCtx, "/home/rodrigao/Videos/reddresswoman.mp4", NULL, NULL) != 0) {
        return -1; // Couldn't open file
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        return -1; // Couldn't find stream information
    }

    // Find the first video stream
    videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    if (videoStream == -1) {
        return -1; // Didn't find a video stream
    }

    // Get a pointer to the codec context for the video stream
    AVCodecParameters *pCodecPar = pFormatCtx->streams[videoStream]->codecpar;
    pCodec = avcodec_find_decoder(pCodecPar->codec_id);
    if (pCodec == NULL) {
        return -1; // Codec not found
    }
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_parameters_to_context(pCodecCtx, pCodecPar) < 0) {
        return -1; // Could not copy codec context
    }


    // Open codec
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        return -1; // Could not open codec
    }

    // Allocate video frame
    pFrame = av_frame_alloc();

    // Create a texture to hold the video frame
    Image videoFrame = {
        .data = malloc(pCodecCtx->width * pCodecCtx->height * 4),
        .width = pCodecCtx->width,
        .height = pCodecCtx->height,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        .mipmaps = 1
    };
    Texture2D videoTexture = LoadTextureFromImage(videoFrame);

    sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                             pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGBA,
                             SWS_BILINEAR, NULL, NULL, NULL);

    SetTargetFPS(0);

    while (!WindowShouldClose()) {
        if (av_read_frame(pFormatCtx, &packet) >= 0) {
            if (packet.stream_index == videoStream) {
                if (avcodec_send_packet(pCodecCtx, &packet) == 0) {
                    while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                        // Convert the image from its native format to RGBA
                        uint8_t *dst[4] = { videoFrame.data, NULL, NULL, NULL };
                        int dst_linesize[4] = { pCodecCtx->width * 4, 0, 0, 0 };
                        sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
                                  pFrame->linesize, 0, pCodecCtx->height,
                                  dst, dst_linesize);

                        UpdateTexture(videoTexture, videoFrame.data);
                    }
                }
            }
            av_packet_unref(&packet);
        }

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexture(videoTexture, 0, 0, WHITE);
        EndDrawing();
    }

    // Free the RGB image
    free(videoFrame.data);
    UnloadTexture(videoTexture);

    // Free the YUV frame
    av_frame_free(&pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    CloseWindow();

    return 0;
}