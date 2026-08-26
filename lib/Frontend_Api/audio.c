#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"

#define AUDIO_SFX_CHANNEL_CAP 8
#define AUDIO_SFX_CHUNK_FRAMES 1024u

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    pthread_t       thread;
    bool            started;
    atomic_uint     generation;

    bool            has_pending;
    uint8_t        *pending_buf;
    const int16_t  *pending_pcm;
    size_t          pending_frames;
    unsigned        pending_rate;
    unsigned        pending_channels;
} audio_sfx_channel_t;

static audio_sfx_channel_t s_channels[AUDIO_SFX_CHANNEL_CAP];

static bool audio_wav_parse_pcm16(const uint8_t *buf, size_t len,
                                   const int16_t **out_pcm, size_t *out_frames,
                                   unsigned *out_rate, unsigned *out_channels) {
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return false;

    uint16_t audio_format = 0, num_channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
    const uint8_t *data_ptr = NULL;
    uint32_t data_len = 0;

    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t *id = buf + pos;
        uint32_t chunk_len;
        memcpy(&chunk_len, buf + pos + 4, 4);
        const uint8_t *chunk = buf + pos + 8;

        if (pos + 8 + chunk_len > len) break;

        if (memcmp(id, "fmt ", 4) == 0 && chunk_len >= 16) {
            memcpy(&audio_format,    chunk + 0,  2);
            memcpy(&num_channels,    chunk + 2,  2);
            memcpy(&sample_rate,     chunk + 4,  4);
            memcpy(&bits_per_sample, chunk + 14, 2);
        } else if (memcmp(id, "data", 4) == 0) {
            data_ptr = chunk;
            data_len = chunk_len;
        }

        pos += 8 + chunk_len + (chunk_len & 1);
    }

    if (audio_format != 1 || bits_per_sample != 16 || !num_channels || !data_ptr)
        return false;

    *out_pcm      = (const int16_t *)data_ptr;
    *out_frames   = data_len / (sizeof(int16_t) * num_channels);
    *out_rate     = sample_rate;
    *out_channels = num_channels;
    return true;
}

static void *audio_sfx_worker(void *arg) {
    audio_sfx_channel_t *ch = arg;

    for (;;) {
        pthread_mutex_lock(&ch->mtx);
        while (!ch->has_pending) pthread_cond_wait(&ch->cv, &ch->mtx);

        uint8_t       *buf      = ch->pending_buf;
        const int16_t *pcm      = ch->pending_pcm;
        size_t         frames   = ch->pending_frames;
        unsigned       rate     = ch->pending_rate;
        unsigned       channels = ch->pending_channels;

        ch->has_pending  = false;
        ch->pending_buf  = NULL;
        unsigned my_gen  = atomic_load(&ch->generation);
        pthread_mutex_unlock(&ch->mtx);

        gamely_daemon_media_audio_configure(rate, channels);

        for (size_t sent = 0; sent < frames && atomic_load(&ch->generation) == my_gen; ) {
            size_t n = frames - sent;
            if (n > AUDIO_SFX_CHUNK_FRAMES) n = AUDIO_SFX_CHUNK_FRAMES;
            gamely_daemon_media_audio_push(pcm + sent * channels, n);
            sent += n;
        }

        free(buf);
    }

    return NULL;
}

static int lua_native_audio_sfx_play(lua_State *L) {
    uint8_t     channel = (uint8_t)luaL_checkinteger(L, 1);
    const char *path    = luaL_checkstring(L, 2);
    lua_settop(L, 0);

    if (channel >= AUDIO_SFX_CHANNEL_CAP)
        return luaL_error(L, "native_audio_sfx_play: invalid channel %d", (int)channel);

    uint8_t *file_buf = NULL;
    size_t   file_len  = 0;
    if (gamely_daemon_fs_read(path, &file_buf, &file_len, NULL, NULL) != 0 || !file_buf)
        return luaL_error(L, "native_audio_sfx_play: could not read '%s'", path);

    const int16_t *pcm      = NULL;
    size_t         frames   = 0;
    unsigned       rate     = 0;
    unsigned       channels = 0;
    if (!audio_wav_parse_pcm16(file_buf, file_len, &pcm, &frames, &rate, &channels)) {
        free(file_buf);
        return luaL_error(L, "native_audio_sfx_play: '%s' is not a 16-bit PCM wav", path);
    }

    audio_sfx_channel_t *ch = &s_channels[channel];

    pthread_mutex_lock(&ch->mtx);

    if (!ch->started) {
        if (pthread_create(&ch->thread, NULL, audio_sfx_worker, ch) != 0) {
            pthread_mutex_unlock(&ch->mtx);
            free(file_buf);
            return luaL_error(L, "native_audio_sfx_play: failed to spawn thread");
        }
        pthread_detach(ch->thread);
        ch->started = true;
    }

    atomic_fetch_add(&ch->generation, 1);
    free(ch->pending_buf);
    ch->pending_buf      = file_buf;
    ch->pending_pcm      = pcm;
    ch->pending_frames   = frames;
    ch->pending_rate     = rate;
    ch->pending_channels = channels;
    ch->has_pending      = true;

    pthread_cond_signal(&ch->cv);
    pthread_mutex_unlock(&ch->mtx);

    return 0;
}

__attribute__((constructor))
static void init() {
    for (int i = 0; i < AUDIO_SFX_CHANNEL_CAP; i++) {
        pthread_mutex_init(&s_channels[i].mtx, NULL);
        pthread_cond_init(&s_channels[i].cv, NULL);
    }
    gecnd_registry("set", "lua_global_func:native_audio_sfx_play", lua_native_audio_sfx_play, NULL);
}
