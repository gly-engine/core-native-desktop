#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
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

typedef struct {
    player_reg_t *player;                 /* anexado ao canal — fonte de state() */
    char         *url;                    /* URL atualmente tocando */
    char         *pending_url;            /* novo source enfileirado */
    bool          pending_play;           /* drain: true=autoplay, false=load pausado */
    bool          want_pause_after_load;  /* aplicar pause() assim que driver vira PLAYING */
} channel_t;

static channel_t s_channels[CHANNEL_CAP];
static bool      s_exiting_stops_issued = false;

static gdmsp_fsm_t channel_state(channel_t *ch, uint8_t idx) {
    if (!ch->player) return GDMSP_FSM_IDLE;
    return ch->player->cbs.state(idx, ch->player->usr);
}

static bool gate_running(gecnd_fsm_t s) {
    return s == GECND_FSM_RUNNING
        || s == GECND_FSM_RUNNING_PERFORMANCE
        || s == GECND_FSM_RUNNING_BACKGROUND
        || s == GECND_FSM_RUNNING_STANDBY
        || s == GECND_FSM_RUNNING_NOGAME;
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

    if (!url) {
        free(ch->pending_url);
        ch->pending_url = NULL;
        if (ch->player && ch->player->cbs.stop)
            ch->player->cbs.stop(channel, ch->player->usr);
        return;
    }

    /* mesma URL já anexada e tocando/carregando/pausada: ignora */
    if (ch->url && strcmp(ch->url, url) == 0) {
        gdmsp_fsm_t st = channel_state(ch, channel);
        if (st == GDMSP_FSM_PLAYING || st == GDMSP_FSM_LOADING || st == GDMSP_FSM_PAUSED)
            return;
    }

    free(ch->pending_url);
    ch->pending_url  = strdup(url);
    ch->pending_play = true;
}

void gamely_daemon_media_playback_play(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];

    if (ch->pending_url) {
        ch->pending_play = true;
        return;
    }
    if (!ch->player) return;

    ch->want_pause_after_load = false;
    gdmsp_fsm_t st = channel_state(ch, channel);
    if (st == GDMSP_FSM_PAUSED && ch->player->cbs.play)
        ch->player->cbs.play(channel, ch->player->usr);
}

void gamely_daemon_media_playback_pause(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];

    if (ch->pending_url) {
        ch->pending_play = false;
        return;
    }
    if (!ch->player) return;

    gdmsp_fsm_t st = channel_state(ch, channel);
    if (st == GDMSP_FSM_PLAYING && ch->player->cbs.pause)
        ch->player->cbs.pause(channel, ch->player->usr);
}

void gamely_daemon_media_playback_stop(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];

    free(ch->pending_url);
    ch->pending_url           = NULL;
    ch->want_pause_after_load = false;
    if (ch->player && ch->player->cbs.stop)
        ch->player->cbs.stop(channel, ch->player->usr);
    /* url/player liberados pelo tick quando driver virar IDLE */
}

void gamely_daemon_media_playback_position(uint8_t channel,
                                            int16_t x, int16_t y,
                                            int16_t w, int16_t h) {
    (void)channel;
    gecnd_filter_set_video_pos(x, y, w, h);
}

void gamely_daemon_media_playback_tick(void) {
    gecnd_t *root = gecnd_get_root();
    gecnd_fsm_t gs = root ? gecnd_get_state(root) : GECND_FSM_BOOT;

    bool in_running = gate_running(gs);
    bool in_exiting = (gs == GECND_FSM_EXITING);

    if (!in_running && !in_exiting) return;

    /* na transição pra EXITING: sinaliza stop em todos e descarta pendings */
    if (in_exiting && !s_exiting_stops_issued) {
        for (int i = 0; i < CHANNEL_CAP; i++) {
            channel_t *ch = &s_channels[i];
            free(ch->pending_url);
            ch->pending_url           = NULL;
            ch->want_pause_after_load = false;
            if (ch->player && ch->player->cbs.stop)
                ch->player->cbs.stop((uint8_t)i, ch->player->usr);
        }
        s_exiting_stops_issued = true;
    }

    for (int i = 0; i < CHANNEL_CAP; i++) {
        channel_t *ch = &s_channels[i];
        gdmsp_fsm_t st = channel_state(ch, (uint8_t)i);

        /* ERROR não é terminal — chama stop, próximo tick vê IDLE */
        if (st == GDMSP_FSM_ERROR) {
            if (ch->player && ch->player->cbs.stop)
                ch->player->cbs.stop((uint8_t)i, ch->player->usr);
            continue;
        }

        /* drena pending quando canal está vazio (só em RUNNING_*) */
        if (in_running && ch->pending_url) {
            if (!ch->player || st == GDMSP_FSM_IDLE) {
                player_reg_t *p = select_player(ch->pending_url);
                if (!p) {
                    fprintf(stderr, "[media] no player for '%s'\n", ch->pending_url);
                    free(ch->pending_url);
                    ch->pending_url = NULL;
                    if (ch->url) { free(ch->url); ch->url = NULL; }
                    ch->player = NULL;
                    continue;
                }
                free(ch->url);
                ch->url                   = ch->pending_url;
                ch->pending_url           = NULL;
                ch->player                = p;
                ch->want_pause_after_load = !ch->pending_play;
                if (p->cbs.start) p->cbs.start((uint8_t)i, ch->url, p->usr);
                continue;
            }
            /* canal ocupado — sinaliza stop e espera */
            if (st != GDMSP_FSM_STOPPING && ch->player->cbs.stop)
                ch->player->cbs.stop((uint8_t)i, ch->player->usr);
            continue;
        }

        /* aplica pause assim que driver entra em PLAYING */
        if (ch->want_pause_after_load && st == GDMSP_FSM_PLAYING) {
            if (ch->player->cbs.pause)
                ch->player->cbs.pause((uint8_t)i, ch->player->usr);
            ch->want_pause_after_load = false;
        }

        /* driver virou IDLE sozinho — libera */
        if (ch->player && st == GDMSP_FSM_IDLE) {
            free(ch->url);
            ch->url    = NULL;
            ch->player = NULL;
            continue;
        }

        if (st == GDMSP_FSM_PLAYING && ch->player && ch->player->cbs.tick)
            ch->player->cbs.tick((uint8_t)i, ch->player->usr);
    }
}

bool gamely_daemon_media_playback_active(void) {
    for (int i = 0; i < CHANNEL_CAP; i++) {
        channel_t *ch = &s_channels[i];
        if (ch->pending_url) return true;
        if (channel_state(ch, (uint8_t)i) != GDMSP_FSM_IDLE) return true;
    }
    return false;
}
