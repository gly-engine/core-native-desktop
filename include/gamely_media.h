#ifndef GAMELY_MEDIA_H
#define GAMELY_MEDIA_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
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
 * Player registry — registro de players por schema composto
 * --------------------------------------------------------------------- */
typedef struct {
    void (*start)(uint8_t channel, const char *url, void *usr);
    void (*stop) (uint8_t channel, void *usr);
    void (*tick) (uint8_t channel, void *usr);   /* NULL = thread própria */
    void (*pause)(uint8_t channel, void *usr);   /* NULL = ignorado */
    void (*play) (uint8_t channel, void *usr);   /* NULL = ignorado */
} gamely_media_player_t;

void gamely_daemon_media_register_player(const char                  *schema,
                                          const gamely_media_player_t *cbs,
                                          void                        *usr);

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
void gamely_daemon_media_playback_tick    (void);

/* -----------------------------------------------------------------------
 * Transmissão — H264/MPEG-TS → clientes HTTP /stream
 * --------------------------------------------------------------------- */
typedef void (*gamely_transmit_cb_t)(const uint8_t *buf, int size, int64_t pts);

void gamely_daemon_media_transmit_callback (gamely_transmit_cb_t cb);
void gamely_daemon_media_transmit_shutdown (void);
bool gamely_daemon_media_transmit_is_online(void);
void gamely_daemon_media_transmit_push     (const uint8_t *rgba, int width, int height);

/* força IDR no próximo frame — usar ao conectar novo cliente sem IDR cache */
void gamely_daemon_media_transmit_force_idr(void);

/* retorna cache do último IDR completo (PAT+PMT + SPS+PPS + slice);
 * *out aponta para buffer interno, válido até o próximo encode_push. */
int  gamely_daemon_media_transmit_get_idr_cache(const uint8_t **out);

/* -----------------------------------------------------------------------
 * Áudio — PCM S16_LE estéreo interleaved
 * --------------------------------------------------------------------- */
typedef void (*gamely_audio_cb_t)(const int16_t *data, size_t frames,
                                   unsigned rate, unsigned channels, void *usr);

void gamely_daemon_media_audio_subscribe (gamely_audio_cb_t cb, void *usr);
void gamely_daemon_media_audio_configure (unsigned rate, unsigned channels);
void gamely_daemon_media_audio_push      (const int16_t *data, size_t frames);

/* -----------------------------------------------------------------------
 * Hardware render readiness — notifica players que o contexto GL está pronto
 * --------------------------------------------------------------------- */
typedef void (*gamely_media_hw_ready_cb)(void);

void gamely_daemon_media_register_hw_ready(gamely_media_hw_ready_cb cb);
void gamely_daemon_media_hw_gl_ready      (void);

/* -----------------------------------------------------------------------
 * Ciclo de vida
 * --------------------------------------------------------------------- */
void gamely_daemon_media_init    (void);
void gamely_daemon_media_shutdown(void);

#endif
