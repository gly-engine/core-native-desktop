/**
 * @file hypervisor.c
 * @todo move all deamon ticks to here
 */
#include "gecnd.h"
#include "gehook.h"

gamely_img_decoded_t gamely_driver_decoder_stb(const uint8_t *data, size_t len);
gamely_img_decoded_t gamely_driver_decoder_spng(const uint8_t *data, size_t len);
extern gamely_media_player_t gamely_player_ffmpeg;

void gecnd_hypervisor(void* loop)
{
    (void) loop;
    gamely_daemon_media_init();

    gamely_daemon_media_register_player(""      , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("file"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("http"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("https" , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("rtsp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("rtmp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("udp"   , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+file"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+http"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+https" , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+rtsp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+rtmp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+udp"   , &gamely_player_ffmpeg, NULL);
}

void gecnd_input_key_cb(const char *name, bool pressed, int port, void *usr)
{
    gecnd_dispatch_key_event((gecnd_t *)usr, name, pressed, port);
}

void gecnd_hypervisor_daemons(gecnd_t *gly)
{
    if (gencd_filter_is_zero_corners())   gecnd_filter_reset_corners();
    if (gencd_filter_is_zero_video_pos()) gecnd_filter_reset_video_pos();
    gamely_daemon_webloop_start(gly->loop);
    gamely_daemon_webclient_start(gly->loop);
    gamely_daemon_webserver_start(gly->loop, gly->port);
    gamely_daemon_fs_start(gly->loop);
    gamely_daemon_db_start();
    gamely_daemon_img_start(gly->loop);
    gly_hook_daemon_img_backend_register();
    gamely_daemon_img_register_decoder("bmp", "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("gif", "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("png", "rgba", true, gamely_driver_decoder_spng);
    gamely_daemon_io_resolver_start();
    gamely_daemon_webclient_img_register();
    gamely_daemon_input_open();
    gamely_daemon_input_subscribe(gecnd_input_key_cb, gly);
}

void gecnd_hypervisor_close_daemons(void)
{
    gamely_daemon_input_close();
}
