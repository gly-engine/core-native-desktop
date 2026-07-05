#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>

#include "gecnd.h"
#include "gdmsp.h"

#define COLOR_CHANNEL_CAP 4

typedef struct {
    bool claimed;
    int  state;
    int  ticks_left;
} color_chan_t;

static color_chan_t s_chans[COLOR_CHANNEL_CAP];

static const struct { const char *name; uint8_t r, g, b; } s_named[] = {
    { "black",   0,   0,   0   },
    { "white",   255, 255, 255 },
    { "red",     255, 0,   0   },
    { "green",   0,   255, 0   },
    { "blue",    0,   0,   255 },
    { "yellow",  255, 255, 0   },
    { "cyan",    0,   255, 255 },
    { "magenta", 255, 0,   255 },
    { "gray",    128, 128, 128 },
};

static bool parse_color(const char *url, uint8_t rgba[4]) {
    rgba[0] = rgba[1] = rgba[2] = 0;
    rgba[3] = 255;

    const char *p = strstr(url, "://");
    if (!p) return false;
    p += 3;

    char spec[64];
    size_t n = 0;
    while (p[n] && p[n] != '?' && n < sizeof(spec) - 1) {
        spec[n] = p[n];
        n++;
    }
    spec[n] = '\0';

    int r, g, b, a;
    if (sscanf(spec, "%d,%d,%d,%d", &r, &g, &b, &a) == 4) {
        rgba[0] = (uint8_t)r;
        rgba[1] = (uint8_t)g;
        rgba[2] = (uint8_t)b;
        rgba[3] = (uint8_t)a;
        return true;
    }

    unsigned int hex;
    if (sscanf(spec, "0x%08x", &hex) == 1 || sscanf(spec, "%08x", &hex) == 1) {
        rgba[0] = (hex >> 24) & 0xFF;
        rgba[1] = (hex >> 16) & 0xFF;
        rgba[2] = (hex >>  8) & 0xFF;
        rgba[3] =  hex        & 0xFF;
        return true;
    }

    for (size_t i = 0; i < sizeof(s_named) / sizeof(s_named[0]); i++) {
        if (strcmp(spec, s_named[i].name) == 0) {
            rgba[0] = s_named[i].r;
            rgba[1] = s_named[i].g;
            rgba[2] = s_named[i].b;
            return true;
        }
    }

    return false;
}

static int parse_ticks(const char *url) {
    const char *k = strstr(url, "ticks=");
    int t;
    if (k && sscanf(k, "ticks=%d", &t) == 1) return t;
    return -1;
}

static gdmsp_fsm_t color_release(uint8_t channel) {
    color_chan_t *c = &s_chans[channel];
    if (c->claimed) {
        gamely_daemon_media_background_release();
        c->claimed = false;
    }
    c->state = GDMSP_FSM_IDLE;
    return GDMSP_FSM_IDLE;
}

static gdmsp_fsm_t color_source(uint8_t channel, const char *url, void *usr) {
    (void)usr;
    if (channel >= COLOR_CHANNEL_CAP) return GDMSP_FSM_ERROR;
    color_chan_t *c = &s_chans[channel];

    color_release(channel);

    uint8_t rgba[4];
    if (!parse_color(url, rgba)) {
        fprintf(stderr, "[color] invalid color spec '%s'\n", url);
        c->state = GDMSP_FSM_ERROR;
        return GDMSP_FSM_ERROR;
    }

    if (!gamely_daemon_media_background_claim()) {
        fprintf(stderr, "[color] background buffer in use\n");
        c->state = GDMSP_FSM_ERROR;
        return GDMSP_FSM_ERROR;
    }
    c->claimed = true;
    c->ticks_left = parse_ticks(url);

    const uint8_t bgrx[4] = { rgba[2], rgba[1], rgba[0], 0xFF };
    gamely_daemon_media_background_push_xrgb8888(bgrx, 1, 1, 4);

    fprintf(stderr, "[color] ch=%u fill #%02X%02X%02X (a=%u)\n",
            channel, rgba[0], rgba[1], rgba[2], rgba[3]);

    c->state = GDMSP_FSM_PLAYING;
    return GDMSP_FSM_PLAYING;
}

static gdmsp_fsm_t color_set(uint8_t channel, gdmsp_cmd_t cmd, gdmsp_value_t value, void *usr) {
    (void)usr;
    if (channel >= COLOR_CHANNEL_CAP) return GDMSP_FSM_IDLE;
    color_chan_t *c = &s_chans[channel];

    switch (cmd) {
        case GDMSP_CMD_RESOURCE:
        case GDMSP_CMD_STOP:
            return color_release(channel);

        case GDMSP_CMD_POSITION:
            gecnd_filter_set_video_pos(value.x, value.y, value.w, value.h);
            break;

        case GDMSP_CMD_TICK:
            if (c->ticks_left > 0 && --c->ticks_left == 0)
                return color_release(channel);
            break;

        case GDMSP_CMD_PLAY:
        case GDMSP_CMD_PAUSE:
        default:
            break;
    }

    return (gdmsp_fsm_t)c->state;
}

static gdmsp_value_t color_get(uint8_t channel, gdmsp_cmd_t cmd, void *usr) {
    (void)channel; (void)cmd; (void)usr;
    gdmsp_value_t value = { -1 };
    return value;
}

static gdmsp_player_t media_player = {
    .src = color_source,
    .set = color_set,
    .get = color_get,
};

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "media_player:background$0", &media_player, NULL);
}