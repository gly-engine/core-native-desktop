#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <uv.h>
#include "gecnd.h"

#include "gefilter.h"

#define CHANNEL_CAP 4

/* ── player selection (registry-backed) ───────────────────────────── */

typedef struct {
    const gamely_media_player_t *player;
} select_ctx_t;

static void select_handler(const char *key, void *value, void *usr) {
    (void)key;
    ((select_ctx_t *)usr)->player = (const gamely_media_player_t *)value;
}

static const gamely_media_player_t *select_player(const char *url) {
    select_ctx_t ctx = { NULL };
    char        *key = malloc(strlen(url) + sizeof("media_player:()"));
    if (!key) return NULL;
    sprintf(key, "media_player:(%s)", url);
    gecnd_registry("get", key, (void *)select_handler, &ctx);
    free(key);
    return ctx.player;
}

/* ── channel state ────────────────────────────────────────────────── */

typedef struct {
    const gamely_media_player_t *player;
    char         *url;
    char         *pending_src;  /* próxima URL a abrir (NULL = nenhuma) */
    gdmsp_cmd_t   pending_cmd;  /* último comando solicitado (NONE = nada a despachar) */
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
    gdmsp_value_t value = {0};
    gdmsp_fsm_t r = ch->player->set(idx, cmd, value, NULL);
    atomic_store(&ch->st, (int)r);
    return r;
}

static void source_runner(void *arg) {
    uint8_t    idx = *(uint8_t *)arg;
    channel_t *ch  = &s_channels[idx];
    gdmsp_fsm_t r  = ch->player->src(idx, ch->url, NULL);
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

void gamely_daemon_media_playback_source(uint8_t channel, const char *url) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];
    free(ch->pending_src);
    if (url) {
        ch->pending_src = strdup(url);
        ch->pending_cmd = GDMSP_CMD_PLAY; /* nova source implica autoplay; sobrescrita
                                             se vier pause/stop depois */
        fprintf(stderr, "[media-pb] ch=%u source set '%s' (cmd=PLAY implícito)\n", channel, url);
    } else {
        ch->pending_src = NULL;
        fprintf(stderr, "[media-pb] ch=%u source cleared\n", channel);
    }
}

void gamely_daemon_media_playback_command(uint8_t channel, gdmsp_cmd_t cmd) {
    if (channel >= CHANNEL_CAP) return;
    s_channels[channel].pending_cmd = cmd; /* last-write-wins */
    fprintf(stderr, "[media-pb] ch=%u command set cmd=%d\n", channel, (int)cmd);
}

gdmsp_fsm_t gamely_daemon_media_playback_get_status(uint8_t channel) {
    if (channel >= CHANNEL_CAP) return GDMSP_FSM_IDLE;
    channel_t *ch = &s_channels[channel];
    if (ch->pending_src || ch->src_active) return GDMSP_FSM_OPENING;
    return ch->player ? ch_st(ch) : GDMSP_FSM_IDLE;
}

int64_t gamely_daemon_media_playback_get_integer(uint8_t channel, gdmsp_cmd_t cmd) {
    if (channel >= CHANNEL_CAP) return -1;
    channel_t *ch = &s_channels[channel];
    if (!ch->player || !ch->player->get) return -1;
    return ch->player->get(channel, cmd, NULL).i64;
}

gdmsp_fsm_t gamely_daemon_media_playback_set_integer(uint8_t channel, gdmsp_cmd_t cmd,
                                                     int64_t value_integer) {
    if (channel >= CHANNEL_CAP) return GDMSP_FSM_IDLE;
    channel_t *ch = &s_channels[channel];
    if (!ch->player || !ch->player->set) return GDMSP_FSM_IDLE;
    gdmsp_value_t value = { value_integer };
    gdmsp_fsm_t r = ch->player->set(channel, cmd, value, NULL);
    atomic_store(&ch->st, (int)r);
    return r;
}

void gamely_daemon_media_playback_position(uint8_t channel,
                                            int16_t x, int16_t y,
                                            int16_t w, int16_t h) {
    if (channel >= CHANNEL_CAP) return;
    channel_t *ch = &s_channels[channel];
    if (!ch->player || !ch->player->set) return;
    gdmsp_value_t value = { .x = x, .y = y, .w = w, .h = h };
    ch->player->set(channel, GDMSP_CMD_POSITION, value, NULL);
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
            free(ch->pending_src); ch->pending_src = NULL;
            ch->pending_cmd = GDMSP_CMD_STOP;
        }
        s_exiting_stops_issued = true;
    }

    for (int i = 0; i < CHANNEL_CAP; i++) {
        channel_t *ch = &s_channels[i];

        /* 1. join source thread quando sair de OPENING */
        if (ch->src_active) {
            if (ch_st(ch) == GDMSP_FSM_OPENING) continue;
            uv_thread_join(&ch->src_thread);
            ch->src_active = false;
            fprintf(stderr, "[media-pb] ch=%d source thread joined, st=%d\n",
                    i, (int)ch_st(ch));
        }

        gdmsp_fsm_t st = ch->player ? ch_st(ch) : GDMSP_FSM_IDLE;

        /* 2. driver em ERROR — força STOP, próximo tick vê IDLE */
        if (st == GDMSP_FSM_ERROR) {
            fprintf(stderr, "[media-pb] ch=%d ERROR → STOP\n", i);
            if (ch->player) channel_cmd(ch, (uint8_t)i, GDMSP_CMD_STOP);
            continue;
        }

        /* 3. STOP pendente — para o driver atual; pending_src é PRESERVADO
         *    (stop seguido de nova source = trocar de player). */
        if (ch->pending_cmd == GDMSP_CMD_STOP) {
            ch->pending_cmd = GDMSP_CMD_NONE;
            if (ch->player && st != GDMSP_FSM_IDLE && st != GDMSP_FSM_STOPPING) {
                fprintf(stderr, "[media-pb] ch=%d dispatch STOP (st=%d)\n", i, (int)st);
                channel_cmd(ch, (uint8_t)i, GDMSP_CMD_STOP);
                continue; /* aguarda driver convergir para IDLE */
            }
        }

        /* 4. drena pending source (só em RUNNING_*) */
        if (in_running && ch->pending_src) {
            /* mesma URL ainda viva no player atual: não re-chama .source()
             * (evita zap/reload desnecessário). O pending_cmd — PLAY implícito
             * do source, ou pause/stop posterior — ainda é aplicado no step 6.
             * Em IDLE/ERROR o player já terminou: deixa re-abrir p/ re-tocar. */
            if (ch->url && strcmp(ch->url, ch->pending_src) == 0
                    && st != GDMSP_FSM_IDLE && st != GDMSP_FSM_ERROR) {
                fprintf(stderr, "[media-pb] ch=%d same URL (st=%d), skip .source()\n",
                        i, (int)st);
                free(ch->pending_src); ch->pending_src = NULL;
            } else {
                const gamely_media_player_t *new_p = select_player(ch->pending_src);
                if (!new_p) {
                    fprintf(stderr, "[media-pb] ch=%d no player for '%s'\n",
                            i, ch->pending_src);
                    free(ch->pending_src); ch->pending_src = NULL;
                    if (ch->url) { free(ch->url); ch->url = NULL; }
                    ch->player = NULL;
                    ch->pending_cmd = GDMSP_CMD_NONE;
                    continue;
                }

                bool same_player = (ch->player == new_p);
                bool diff_player = (ch->player && !same_player);

                /* Troca de URL/player com stream ativo: para e só abre .source() em
                 * IDLE. Em STOPPING não pode dar continue aqui — step 7 TICK chama
                 * av_reap quando o worker ffmpeg termina. */
                if ((same_player || diff_player) && st != GDMSP_FSM_IDLE) {
                    if (st != GDMSP_FSM_STOPPING) {
                        if (diff_player) {
                            fprintf(stderr,
                                    "[media-pb] ch=%d switching player → STOP (st=%d)\n",
                                    i, (int)st);
                            channel_cmd(ch, (uint8_t)i, GDMSP_CMD_STOP);
                        } else {
                            fprintf(stderr,
                                    "[media-pb] ch=%d same player, stop before new source (st=%d)\n",
                                    i, (int)st);
                            channel_cmd(ch, (uint8_t)i, GDMSP_CMD_RESOURCE);
                        }
                        continue; /* pending_src preservado */
                    }
                    /* STOPPING: segue para step 5/7 (TICK), sem spawn ainda */
                } else {

                fprintf(stderr, "[media-pb] ch=%d spawn .source('%s') %s\n",
                        i, ch->pending_src, same_player ? "(same player)" : "(new player)");
                free(ch->url);
                ch->url         = ch->pending_src;
                ch->pending_src = NULL;
                ch->player      = new_p;

                atomic_store(&ch->st, (int)GDMSP_FSM_OPENING);
                s_src_idx[i] = (uint8_t)i;
                ch->src_active = true;
                if (uv_thread_create(&ch->src_thread, source_runner, &s_src_idx[i]) != 0) {
                    ch->src_active = false;
                    atomic_store(&ch->st, (int)GDMSP_FSM_ERROR);
                    fprintf(stderr, "[media-pb] ch=%d failed to spawn source thread\n", i);
                }
                continue;
                } /* else: pronto para spawn */
            }
        }

        /* 5. driver virou IDLE — libera; descarta PLAY/PAUSE só se também não
         *    houver pending_src (senão o cmd se aplica ao próximo player). */
        if (ch->player && st == GDMSP_FSM_IDLE) {
            fprintf(stderr, "[media-pb] ch=%d player IDLE, release\n", i);
            free(ch->url); ch->url = NULL;
            ch->player = NULL;
            if (!ch->pending_src
                    && (ch->pending_cmd == GDMSP_CMD_PLAY
                        || ch->pending_cmd == GDMSP_CMD_PAUSE))
                ch->pending_cmd = GDMSP_CMD_NONE;
            continue;
        }

        /* 6. aplica pending_cmd cru — driver conhece o próprio estado interno */
        if (ch->player && ch->pending_cmd != GDMSP_CMD_NONE) {
            gdmsp_cmd_t cmd = ch->pending_cmd;
            ch->pending_cmd = GDMSP_CMD_NONE;
            fprintf(stderr, "[media-pb] ch=%d dispatch cmd=%d (st=%d)\n",
                    i, (int)cmd, (int)st);
            channel_cmd(ch, (uint8_t)i, cmd);
            continue;
        }

        /* 7. tick normal */
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

__attribute__((constructor))
static void register_playback_functions(void) {
    gecnd_registry("set", "function:gamely_daemon_media_playback_source", (void *)gamely_daemon_media_playback_source, NULL);
}
