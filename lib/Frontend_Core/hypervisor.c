/**
 * @file hypervisor.c
 * @todo move all daemon ticks to here
 */
#include <string.h>
#include "gecnd.h"
#include "gehook.h"
#include <stdio.h>
gamely_img_decoded_t gamely_driver_decoder_stb(const uint8_t *data, size_t len);
gamely_img_decoded_t gamely_driver_decoder_spng(const uint8_t *data, size_t len);
void gamely_resolver_image_file(const char *url, void *schema_usr, gamely_img_on_fetch_cb on_done, void *usr);
void gamely_resolver_image_http(const char *url, void *schema_usr, gamely_img_on_fetch_cb on_done, void *usr);
extern gamely_media_player_t gamely_player_ffmpeg;

static gecnd_display_t g_display;

gecnd_display_t *gecnd_get_display(void) {
    return &g_display;
}

static void gamely_resolver_image_base_url(const char *url, void *usr,
                                            gamely_img_on_fetch_cb on_done, void *on_done_usr) {
    char full[768];
    snprintf(full, sizeof(full), "%s%s", (const char *)usr, url);
    gamely_resolver_image_http(full, NULL, on_done, on_done_usr);
}

/**
 * @todo use defined to register specific drivers by cmake options / platforms
 */
void gamely_hypervisor_init(gecnd_t *gly) {
    if (g_display.window_width == 0 || g_display.window_height == 0) {
        g_display.window_width  = gly->width;
        g_display.window_height = gly->height;
    }

    if (gencd_filter_is_zero_corners())   gecnd_filter_reset_corners();
    if (gencd_filter_is_zero_video_pos()) gecnd_filter_reset_video_pos();

    gamely_daemon_db_start();
    gamely_daemon_media_init();
    gamely_daemon_fs_start(gly->loop);
    gamely_daemon_webloop_start(gly->loop);
    gamely_daemon_webclient_start(gly->loop);
    gamely_daemon_webserver_start(gly->loop, g_display.port);
    gamely_daemon_img_start(gly->loop);

    gamely_daemon_img_opengl_register();
    gamely_daemon_img_register_decoder("jpeg", "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("jpg",  "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("bmp",  "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("gif",  "rgba", true, gamely_driver_decoder_stb);
    gamely_daemon_img_register_decoder("png",  "rgba", true, gamely_driver_decoder_spng);
    if (g_display.game_base_url[0])
        gamely_daemon_img_register_schema("", gamely_resolver_image_base_url, g_display.game_base_url);
    else
        gamely_daemon_img_register_schema("", gamely_resolver_image_file, NULL);
    gamely_daemon_img_register_schema("file://",  gamely_resolver_image_file, NULL);
    gamely_daemon_img_register_schema("http://",  gamely_resolver_image_http, NULL);
    gamely_daemon_img_register_schema("https://", gamely_resolver_image_http, NULL);

/**
 * @todo microslop??
 */
#if !defined(_WIN32)
    gamely_daemon_media_register_player(""             , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("file"         , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("http"         , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("https"        , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("rtsp"         , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("rtmp"         , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("udp"          , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+file"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+http"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+https" , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+rtsp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+rtmp"  , &gamely_player_ffmpeg, NULL);
    gamely_daemon_media_register_player("ffmpeg+udp"   , &gamely_player_ffmpeg, NULL);
#endif

    gamely_daemon_input_subscribe(gecnd_dispatch_key_event, gly);
}

void gamely_hypervisor_tick(void) {
    gamely_daemon_input_tick();
    gamely_daemon_fs_tick();
}

void gamely_hypervisor_exit(void) {
    gamely_daemon_input_close();
}
