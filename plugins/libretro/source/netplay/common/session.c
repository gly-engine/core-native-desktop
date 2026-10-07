/**
 * @file plugins/libretro/source/netplay/common/session.c
 * @brief Players, inputs by frame and the core's input during a netplay
 * session, shared by the client and the host.
 */
#include <string.h>
#include <stdlib.h>

#include "netplay/common/session.h"

np_session_t np_session;
unsigned     np_input_delay = 1;

uint32_t np_rd32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void np_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

bool np_reserve(uint8_t **buf, size_t *cap, size_t need) {
    if (need <= *cap) return true;
    size_t ncap = *cap ? *cap : 4096;
    while (ncap < need) ncap *= 2;
    uint8_t *nb = realloc(*buf, ncap);
    if (!nb) return false;
    *buf = nb;
    *cap = ncap;
    return true;
}

uint32_t netplay_crc32(const void *data, size_t size) {
    static uint32_t table[256];
    const uint8_t *p = data;
    uint32_t crc = 0xffffffffu;

    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    while (size--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

uint32_t np_platform(void) {
    const uint16_t one = 1;
    const uint32_t big = *(const uint8_t *)&one == 0;
    return big << 30 | (uint32_t)sizeof(size_t) << 15 | (uint32_t)sizeof(long);
}

void np_session_reset(void) {
    memset(&np_session, 0, sizeof(np_session));
    np_session.rewind = NP_NO_FRAME;
}

void np_session_rewind(uint32_t frame) {
    if (frame < np_session.self_frame && frame < np_session.rewind)
        np_session.rewind = frame;
}

unsigned np_words_for(uint32_t devices) {
    unsigned words = 0;
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++)
        if (devices & (1u << d)) words += np_device_words(np_session.devices[d]);
    return words;
}

bool np_player_at(uint32_t client, uint32_t frame) {
    const np_player_t *p = &np_session.players[client];
    return p->devices && frame >= p->from && frame < p->until;
}

void np_store_input(uint32_t client, uint32_t frame, const uint8_t *data, unsigned words) {
    np_input_t *in = &np_session.inputs[client][frame % NP_RING];
    uint32_t real[NP_MAX_WORDS] = {0};

    for (unsigned i = 0; i < words && i < NP_MAX_WORDS; i++)
        real[i] = np_rd32(data + 4 * i);
    /* a frame that ran with a wrong guess runs again */
    if (in->set && in->guess && in->frame == frame && memcmp(in->words, real, sizeof(real)))
        np_session_rewind(frame);
    in->frame = frame;
    in->set = true;
    in->guess = false;
    memcpy(in->words, real, sizeof(real));
}

const np_input_t *np_input(uint32_t client, uint32_t frame) {
    const np_input_t *in = &np_session.inputs[client][frame % NP_RING];
    return in->set && !in->guess && in->frame == frame ? in : NULL;
}

void np_guess_inputs(uint32_t frame) {
    for (uint32_t c = 0; c < NP_MAX_CLIENTS; c++) {
        np_input_t *in = &np_session.inputs[c][frame % NP_RING];
        const np_input_t *last = NULL;

        if (!np_player_at(c, frame) || np_input(c, frame)) continue;
        /* the player keeps doing what it did */
        for (uint32_t back = 1; back < 64 && back <= frame && !last; back++) {
            const np_input_t *prev = &np_session.inputs[c][(frame - back) % NP_RING];
            if (prev->set && prev->frame == frame - back) last = prev;
        }
        in->frame = frame;
        in->set = true;
        in->guess = true;
        if (last) memcpy(in->words, last->words, sizeof(in->words));
        else memset(in->words, 0, sizeof(in->words));
    }
}

bool np_inputs_ready(uint32_t frame) {
    for (uint32_t c = 0; c < NP_MAX_CLIENTS; c++)
        if (np_player_at(c, frame) && !np_input(c, frame))
            return false;
    return true;
}

unsigned np_local_input(uint32_t client, const netplay_core_t *core, bool live, uint8_t *out) {
    const uint32_t devices = np_session.players[client].devices;
    unsigned words = 0, local = 0;

    for (unsigned d = 0; d < NP_MAX_DEVICES && words < NP_MAX_WORDS; d++) {
        if (!(devices & (1u << d))) continue;
        const unsigned n = np_device_words(np_session.devices[d]);
        for (unsigned w = 0; w < n && words < NP_MAX_WORDS; w++, words++) {
            uint32_t v = 0;
            if (live && w == 0 && core->local_buttons)
                v = core->local_buttons(local) & 0xffff;
            np_wr32(out + 4 * words, v);
        }
        local++;
    }
    return 4 * words;
}

int16_t np_session_input(unsigned port, unsigned device, unsigned index, unsigned id) {
    const uint32_t frame = np_session.run_frame;
    uint32_t buttons = 0;

    if (port >= NP_MAX_DEVICES) return 0;
    for (uint32_t c = 0; c < NP_MAX_CLIENTS; c++) {
        const np_player_t *p = &np_session.players[c];
        const np_input_t *in;
        unsigned offset = 0;

        if (!(p->devices & (1u << port)) || !np_player_at(c, frame)) continue;
        in = &np_session.inputs[c][frame % NP_RING];  /* real or guessed */
        if (!in->set || in->frame != frame) continue;
        for (unsigned d = 0; d < port; d++)
            if (p->devices & (1u << d)) offset += np_device_words(np_session.devices[d]);
        if (offset < NP_MAX_WORDS) buttons |= in->words[offset];  /* shared ports OR */
    }

    /* joypad buttons, also the buttons of an analog pad; sticks: TODO */
    if ((device & 0xff) != 1 && (device & 0xff) != 5) return 0;
    if ((device & 0xff) == 5 && index) return 0;
    if (id == 256) return (int16_t)(buttons & 0xffff);  /* RETRO_DEVICE_ID_JOYPAD_MASK */
    return id < 16 ? (int16_t)((buttons >> id) & 1) : 0;
}
