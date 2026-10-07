/**
 * @file plugins/libretro/source/netplay/lobby/relay.c
 * @brief Relay ids and the base64 they are listed in.
 */
#include <string.h>

#include "netplay/lobby/relay.h"
#include "netplay/common/session.h"

static const char np_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void np_relay_id(uint8_t out[NP_RELAY_ID_SIZE], uint32_t magic, const uint8_t *unique) {
    np_wr32(out, magic);
    if (unique) memcpy(out + 4, unique, NP_RELAY_UNIQUE_SIZE);
    else memset(out + 4, 0, NP_RELAY_UNIQUE_SIZE);
}

void np_relay_encode(const uint8_t unique[NP_RELAY_UNIQUE_SIZE], char out[17]) {
    unsigned o = 0;

    /* 12 bytes are 4 groups of 3: 16 characters, no padding */
    for (unsigned i = 0; i < NP_RELAY_UNIQUE_SIZE; i += 3) {
        const uint32_t v = (uint32_t)unique[i] << 16 | (uint32_t)unique[i + 1] << 8 | unique[i + 2];
        out[o++] = np_b64[(v >> 18) & 63];
        out[o++] = np_b64[(v >> 12) & 63];
        out[o++] = np_b64[(v >> 6) & 63];
        out[o++] = np_b64[v & 63];
    }
    out[o] = '\0';
}

bool np_relay_decode(const char *text, uint8_t unique[NP_RELAY_UNIQUE_SIZE]) {
    unsigned n = 0;
    uint32_t acc = 0;
    unsigned bits = 0;

    if (!text) return false;
    for (const char *p = text; *p && *p != '='; p++) {
        const char *c = strchr(np_b64, *p == '-' ? '+' : *p == '_' ? '/' : *p);
        if (!c || !*p) return false;
        acc = acc << 6 | (uint32_t)(c - np_b64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n == NP_RELAY_UNIQUE_SIZE) return false;
            unique[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n == NP_RELAY_UNIQUE_SIZE;
}
