#ifndef GAMELY_MEDIA_H
#define GAMELY_MEDIA_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include "gebuffer.h"

/* -----------------------------------------------------------------------
 * Background buffer — triple buffer (libretro ou playback → OpenGL)
 * --------------------------------------------------------------------- */
bool        gamely_daemon_media_background_is_available (void);
bool        gamely_daemon_media_background_claim        (void);
void        gamely_daemon_media_background_release      (void);

void        gamely_daemon_media_background_push_yuv420  (const uint8_t *y,
                                                          const uint8_t *u,
                                                          const uint8_t *v,
                                                          int w, int h,
                                                          int y_stride, int uv_stride);
void        gamely_daemon_media_background_push_xrgb8888(const uint8_t *data,
                                                          int w, int h, int pitch);
void        gamely_daemon_media_background_push_rgb565  (const uint8_t *data,
                                                          int w, int h, int pitch);

MediaFrame *gamely_daemon_media_background_get_frame    (void);
bool        gamely_daemon_media_background_check_update (atomic_int *local_counter);

/* -----------------------------------------------------------------------
 * Playback — reprodução de vídeo por canal
 * --------------------------------------------------------------------- */
void gamely_daemon_media_playback_source  (uint8_t channel, const char *url);
void gamely_daemon_media_playback_play    (uint8_t channel);
void gamely_daemon_media_playback_pause   (uint8_t channel);
void gamely_daemon_media_playback_stop    (uint8_t channel);
void gamely_daemon_media_playback_position(uint8_t channel,
                                            int16_t x, int16_t y,
                                            int16_t w, int16_t h);

/* -----------------------------------------------------------------------
 * Transmissão — H264/MPEG-TS → clientes HTTP /stream
 * --------------------------------------------------------------------- */
typedef void (*gamely_transmit_cb_t)(const uint8_t *buf, int size, int64_t pts);

void gamely_daemon_media_transmit_callback (gamely_transmit_cb_t cb);
void gamely_daemon_media_transmit_shutdown (void);
bool gamely_daemon_media_transmit_is_online(void);
void gamely_daemon_media_transmit_push     (const uint8_t *rgba, int width, int height);

/* -----------------------------------------------------------------------
 * Ciclo de vida
 * --------------------------------------------------------------------- */
void gamely_daemon_media_init    (void);
void gamely_daemon_media_shutdown(void);

#endif
