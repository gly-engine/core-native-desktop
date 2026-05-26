#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <uv.h>
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
    player_reg_t *player;
    char         *url;
    char         *pending_src;  /* próximo source a abrir */
    gdmsp_cmd_t   pending_cmd;  /* NONE=autoplay, PAUSE=carregar pausado, STOP=cancelar */
    atomic_int    st;           /* gdmsp_fsm_t — OPENING enquanto source thread roda */
    uv_thread_t   src_thread;
    bool          src_active;   /* join pendente */
} channel_t;

static channel_t s_channels[CHANNEL_CAP];
static uint8_t   s_src_idx[CHANNEL_CAP];
static bool      s_exiting_stops_issued = false;

static gdmsp_fsm_t ch_st(channel_t *ch) {
    return (gdmsp_fsm_t)atomic_load(&ch->st);
}

static gdmsp_fsm_t channel_cmd(channel_t *ch, uint8_t idx, gdmsp_cmd_t cmd) {
    gdmsp_fsm_t r = ch->player->cbs.command(idx, cmd, ch->player->usr);
    atomic_store(&ch->st, (int)r);
    return r;
}

static void source_runner(void *arg) {
    uint8_t    idx = *(uint8_t *)arg;
    channel_t *ch  = &s_channels[idx];
    gdmsp_fsm_t r  = ch->player->cbs.source(idx, ch->url, ch->player->usr);
    atomic_store(&ch->st, (int)r);
    /* OPENING → r: tick detecta a transição e faz o join */
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
        free(ch->pending_src);
        ch->pending_src = NULL;
        ch->pending_cmd = ch->src_active ? GDMSP_CMD_STOP : GDMSP_CMD_NONE;
        if (!ch->src_active && ch->player)
            channel_cmd(ch, channel, GDMSP_CMD_STOP);
        return;
    }

    /* mesma URL já ativa: ignora */
    if (ch->url && strcmp(ch->url, url) == 0) {
        gdmsp_fsm_t st = ch_st(ch);
        if (st == GDMSP_FSM_OPENING || st == GDMSP_FSM_PLAYING
                || st == GDMSP_FSM_LOADING || st == GDMSP_FSM_PAUSED)
            return;
    }

    free(ch->pending_src);
    ch->pending_src = strdup(url);
    ch->pending_cmd = GDMSP_CMD_NONE; /* autoplay por padrão */
}

void gamely_daemon_media_playback_play(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];

    if (ch->pending_src || ch->src_active) {
        if (ch->pending_cmd == GDMSP_CMD_PAUSE) ch->pending_cmd = GDMSP_CMD_NONE;
        return;
    }
    if (!ch->player) return;

    if (ch_st(ch) == GDMSP_FSM_PAUSED)
        channel_cmd(ch, channel, GDMSP_CMD_PLAY);
}

void gamely_daemon_media_playback_pause(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];

    if (ch->pending_src || ch->src_active) {
        ch->pending_cmd = GDMSP_CMD_PAUSE;
        return;
    }
    if (!ch->player) return;

    if (ch_st(ch) == GDMSP_FSM_PLAYING)
        channel_cmd(ch, channel, GDMSP_CMD_PAUSE);
}

void gamely_daemon_media_playback_stop(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];

    free(ch->pending_src);
    ch->pending_src = NULL;
    ch->pending_cmd = ch->src_active ? GDMSP_CMD_STOP : GDMSP_CMD_NONE;
    if (!ch->src_active && ch->player)
        channel_cmd(ch, channel, GDMSP_CMD_STOP);
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

    if (in_exiting && !s_exiting_stops_issued) {
        for (int i = 0; i < CHANNEL_CAP; i++) {
            channel_t *ch = &s_channels[i];
            free(ch->pending_src);
            ch->pending_src = NULL;
            ch->pending_cmd = ch->src_active ? GDMSP_CMD_STOP : GDMSP_CMD_NONE;
            if (!ch->src_active && ch->player)
                channel_cmd(ch, (uint8_t)i, GDMSP_CMD_STOP);
        }
        s_exiting_stops_issued = true;
    }

    for (int i = 0; i < CHANNEL_CAP; i++) {
        channel_t *ch = &s_channels[i];

        /* join quando source thread terminar (OPENING → qualquer outro estado) */
        if (ch->src_active) {
            if (ch_st(ch) == GDMSP_FSM_OPENING) continue;
            uv_thread_join(&ch->src_thread);
            ch->src_active = false;

            /* aplica comando diferido */
            if (ch->pending_cmd == GDMSP_CMD_STOP) {
                ch->pending_cmd = GDMSP_CMD_NONE;
                channel_cmd(ch, (uint8_t)i, GDMSP_CMD_STOP);
                continue;
            }
            if (ch->pending_cmd == GDMSP_CMD_PAUSE && ch_st(ch) == GDMSP_FSM_PLAYING) {
                ch->pending_cmd = GDMSP_CMD_NONE;
                channel_cmd(ch, (uint8_t)i, GDMSP_CMD_PAUSE);
            }
            ch->pending_cmd = GDMSP_CMD_NONE;
            /* fall-through para tick normal */
        }

        gdmsp_fsm_t st = ch->player ? ch_st(ch) : GDMSP_FSM_IDLE;

        /* ERROR → stop; próximo tick vê IDLE */
        if (st == GDMSP_FSM_ERROR) {
            if (ch->player) channel_cmd(ch, (uint8_t)i, GDMSP_CMD_STOP);
            continue;
        }

        /* drena pending source (só em RUNNING_*) */
        if (in_running && ch->pending_src) {
            /* avisa player atual enquanto não estiver IDLE (idempotente) */
            if (ch->player && st != GDMSP_FSM_IDLE) {
                channel_cmd(ch, (uint8_t)i, GDMSP_CMD_RESOURCE);
                st = ch_st(ch);
                if (st != GDMSP_FSM_IDLE) continue;
            }

            player_reg_t *p = select_player(ch->pending_src);
            if (!p) {
                fprintf(stderr, "[media] no player for '%s'\n", ch->pending_src);
                free(ch->pending_src); ch->pending_src = NULL;
                if (ch->url) { free(ch->url); ch->url = NULL; }
                ch->player = NULL;
                continue;
            }
            free(ch->url);
            ch->url        = ch->pending_src;
            ch->pending_src = NULL;
            ch->player     = p;

            atomic_store(&ch->st, (int)GDMSP_FSM_OPENING);
            s_src_idx[i] = (uint8_t)i;
            ch->src_active = true;
            if (uv_thread_create(&ch->src_thread, source_runner, &s_src_idx[i]) != 0) {
                ch->src_active = false;
                atomic_store(&ch->st, (int)GDMSP_FSM_ERROR);
                fprintf(stderr, "[media] failed to spawn source thread\n");
            }
            continue;
        }

        /* driver virou IDLE — libera */
        if (ch->player && st == GDMSP_FSM_IDLE) {
            free(ch->url);
            ch->url    = NULL;
            ch->player = NULL;
            continue;
        }

        /* tick normal */
        if (ch->player) channel_cmd(ch, (uint8_t)i, GDMSP_CMD_TICK);
    }
}

bool gamely_daemon_media_playback_active(void) {
    for (int i = 0; i < CHANNEL_CAP; i++) {
        channel_t *ch = &s_channels[i];
        if (ch->pending_src || ch->src_active) return true;
        if (ch->player && ch_st(ch) != GDMSP_FSM_IDLE) return true;
    }
    return false;
}
