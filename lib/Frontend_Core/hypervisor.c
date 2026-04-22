/**
 * @file hypervisor.c
 * @todo move all daemon registers to here
 * @todo move all deamon ticks to here
 */
#include "gecnd.h"
#include "gamely_media.h"

gamely_img_decoded_t gamely_driver_decoder_stb(const uint8_t *data, size_t len);
gamely_img_decoded_t gamely_driver_decoder_spng(const uint8_t *data, size_t len);
extern gamely_media_player_t gamely_player_ffmpeg;

void gecnd_hypervisor(void* loop)
{
    (void) loop;
    gamely_daemon_img_register_decoder("bmp", "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("gif", "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("png", "rgba", true, gamely_driver_decoder_spng);

    gamely_daemon_media_register_player(""      , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("file"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("http"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("https" , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("rtsp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("rtmp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("udp"   , &gamely_player_ffmpeg, NULL);
}
