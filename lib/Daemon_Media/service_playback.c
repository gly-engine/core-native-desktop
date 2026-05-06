#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gecnd.h"

#include "gefilter.h"

#define PLAYER_CAP  32
#define CHANNEL_CAP 4
#define TOKEN_CAP   8
#define TOKEN_LEN   32

/* ── player registry ──────────────────────────────────────────────── */

typedef struct {
    char                  named[TOKEN_CAP][TOKEN_LEN];
    int                   named_n;
    int                   wild_n;
    gamely_media_player_t cbs;
    void                 *usr;
} player_reg_t;

static player_reg_t s_players[PLAYER_CAP];
static int          s_player_n = 0;

static void parse_schema(const char *schema,
                          char named[][TOKEN_LEN], int *named_n, int *wild_n) {
    *named_n = 0;
    *wild_n  = 0;
    char buf[256];
    strncpy(buf, schema, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, "+");
    while (tok) {
        if (strcmp(tok, "?") == 0) {
            (*wild_n)++;
        } else if (*named_n < TOKEN_CAP) {
            strncpy(named[*named_n], tok, TOKEN_LEN - 1);
            named[(*named_n)++][TOKEN_LEN - 1] = '\0';
        }
        tok = strtok(NULL, "+");
    }
}

static int parse_url_schema(const char *url,
                              char tokens[][TOKEN_LEN], int cap) {
    (void)cap;
    const char *sep = strstr(url, "://");
    int n = 0;
    if (!sep) return 0;
    char buf[256];
    size_t slen = (size_t)(sep - url);
    if (slen >= sizeof(buf)) slen = sizeof(buf) - 1;
    memcpy(buf, url, slen);
    buf[slen] = '\0';
    char *tok = strtok(buf, "+");
    while (tok && n < cap) {
        strncpy(tokens[n], tok, TOKEN_LEN - 1);
        tokens[n++][TOKEN_LEN - 1] = '\0';
        tok = strtok(NULL, "+");
    }
    return n;
}

static bool has_token(const char tokens[][TOKEN_LEN], int n, const char *t) {
    for (int i = 0; i < n; i++)
        if (strcmp(tokens[i], t) == 0) return true;
    return false;
}

static int count_extras(const char url_tok[][TOKEN_LEN], int url_n,
                         const char named[][TOKEN_LEN],   int named_n) {
    int extras = 0;
    for (int i = 0; i < url_n; i++)
        if (!has_token(named, named_n, url_tok[i])) extras++;
    return extras;
}

/* ffmpeg+file://x → file://x  |  aui+rtsp://x → rtsp://x  |  rtsp://x → rtsp://x */
static const char *strip_driver_prefix(const char *url) {
    const char *sep  = strstr(url, "://");
    if (!sep) return url;
    const char *plus = memchr(url, '+', (size_t)(sep - url));
    return plus ? plus + 1 : url;
}

static player_reg_t *select_player(const char *url) {
    char url_tok[TOKEN_CAP][TOKEN_LEN];
    int  url_n = parse_url_schema(url, url_tok, TOKEN_CAP);

    player_reg_t *best       = NULL;
    int           best_score = -1;

    for (int i = 0; i < s_player_n; i++) {
        player_reg_t *p = &s_players[i];

        bool all_present = true;
        for (int j = 0; j < p->named_n; j++) {
            if (!has_token(url_tok, url_n, p->named[j])) {
                all_present = false;
                break;
            }
        }
        if (!all_present) continue;

        if (count_extras(url_tok, url_n, p->named, p->named_n) != p->wild_n)
            continue;

        if (p->named_n >= best_score) {
            best       = p;
            best_score = p->named_n;
        }
    }
    return best;
}

/* ── channel state ────────────────────────────────────────────────── */

typedef enum { CH_IDLE, CH_LOADING, CH_PLAYING, CH_PAUSED } ch_state_t;

typedef struct {
    ch_state_t    state;
    player_reg_t *player;
    char         *url;
} channel_t;

static channel_t s_channels[CHANNEL_CAP];

static void channel_stop(channel_t *ch, uint8_t idx) {
    if (ch->state == CH_IDLE || !ch->player) return;
    if (ch->player->cbs.stop)
        ch->player->cbs.stop(idx, ch->player->usr);
    free(ch->url);
    ch->url    = NULL;
    ch->player = NULL;
    ch->state  = CH_IDLE;
}

/* ── public API ───────────────────────────────────────────────────── */

void gamely_daemon_media_register_player(const char                  *schema,
                                          const gamely_media_player_t *cbs,
                                          void                        *usr) {
    if (s_player_n >= PLAYER_CAP) return;
    player_reg_t *p = &s_players[s_player_n++];
    parse_schema(schema, p->named, &p->named_n, &p->wild_n);
    p->cbs = *cbs;
    p->usr = usr;
}

void gamely_daemon_media_playback_source(uint8_t channel, const char *url) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];

    if (url && ch->url && ch->state != CH_IDLE && strcmp(ch->url, url) == 0)
        return;

    if (!url) {
        channel_stop(ch, channel);
        return;
    }

    player_reg_t *p = select_player(url);
    if (!p) {
        fprintf(stderr, "[media] no player for '%s'\n", url);
        channel_stop(ch, channel);
        return;
    }

    /* Soft switch: same player handles old → new source without a hard stop.
       Driver's start() must be re-entrant and reuse what it can internally. */
    if (ch->state != CH_IDLE && ch->player == p) {
        free(ch->url);
        ch->url   = strdup(url);
        ch->state = CH_LOADING;
        if (p->cbs.start) p->cbs.start(channel, url, p->usr);
        if (ch->state == CH_LOADING) ch->state = CH_PLAYING;
        return;
    }

    channel_stop(ch, channel);

    ch->player = p;
    ch->url    = strdup(url);
    ch->state  = CH_LOADING;
    if (p->cbs.start) p->cbs.start(channel, url, p->usr);
    if (ch->state == CH_LOADING) ch->state = CH_PLAYING;
}

void gamely_daemon_media_playback_play(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];
    if (ch->state != CH_PAUSED || !ch->player) return;
    if (ch->player->cbs.play) ch->player->cbs.play(channel, ch->player->usr);
    ch->state = CH_PLAYING;
}

void gamely_daemon_media_playback_pause(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];
    if (ch->state != CH_PLAYING || !ch->player) return;
    if (ch->player->cbs.pause) ch->player->cbs.pause(channel, ch->player->usr);
    ch->state = CH_PAUSED;
}

void gamely_daemon_media_playback_stop(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_stop(&s_channels[channel], channel);
}

void gamely_daemon_media_playback_position(uint8_t channel,
                                            int16_t x, int16_t y,
                                            int16_t w, int16_t h) {
    (void)channel;
    gecnd_filter_set_video_pos(x, y, w, h);
}

void gamely_daemon_media_playback_tick(void) {
    for (int i = 0; i < CHANNEL_CAP; i++) {
        channel_t *ch = &s_channels[i];
        if (ch->state == CH_PLAYING && ch->player && ch->player->cbs.tick)
            ch->player->cbs.tick((uint8_t)i, ch->player->usr);
    }
}

bool gamely_daemon_media_playback_active(void) {
    for (int i = 0; i < CHANNEL_CAP; i++)
        if (s_channels[i].state != CH_IDLE) return true;
    return false;
}
