/**
 * @file plugins/libretro/source/netplay/host/host.c
 * @brief Netplay host for RetroArch compatible clients (protocol.h).
 *
 * Handshake, host side: take the client's header and answer ours (no
 * password, the protocol and compression both speak), exchange NICK, send
 * our INFO and check the client's, then SYNC (frame, devices, who controls
 * them, SRAM) followed by our savestate at that frame. A client starts as
 * a spectator; PLAY gives it the first free port, announced to everyone
 * with MODE.
 *
 * The host is the session's clock: one frame per tick, guessing the
 * clients' inputs not here yet and going back when a guess was wrong
 * (rollback.h). Its own input goes to every client np_input_delay frames
 * ahead; a client's is forwarded to the others as it arrives. A client
 * joins at the last final frame, with that frame's state and the inputs
 * since. A client that leaves stops counting from the first frame it did
 * not send.
 *
 * A core with its own netcode (netplay_core_t.packets) gets none of that:
 * the host runs it freely and carries its packets, delivering the ones
 * for the host and passing on the ones for other clients.
 */
#include "netplay/host/host.h"

#ifdef _WIN32

/* TODO: Winsock; until then netplay is off on Windows */
bool netplay_host_start(uint16_t port, const char *nick, const netplay_core_t *core) {
    (void)port; (void)nick; (void)core;
    return false;
}
void netplay_host_announce(const char *game_name) { (void)game_name; }
bool netplay_host_relay(const char *handle) { (void)handle; return false; }
void netplay_host_stop(void) {}
bool netplay_host_active(void) { return false; }
void netplay_host_tick(void) {}

#else

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

#ifdef GECND_NETPLAY_ZLIB
#include <zlib.h>
#endif

#include "netplay/common/rollback.h"
#include "netplay/lobby/lobby.h"
#include "netplay/lobby/relay.h"

#define rd32    np_rd32
#define wr32    np_wr32
#define reserve np_reserve

/** @brief Connections at once; client numbers are 1..NP_HOST_CONNS. */
#define NP_HOST_CONNS 8

/** @brief Ports given to players, the host's (0) included. */
#define NP_HOST_PORTS 4

/** @brief Seconds a client may take to finish the handshake. */
#define NP_HANDSHAKE_TIMEOUT 15

/** @brief MODE_REFUSED reasons: every port is taken; anything else. */
#define NP_REFUSED_NO_SLOTS 2
#define NP_REFUSED_OTHER    4

/** @brief RETRO_DEVICE_JOYPAD, what every port holds. */
#define NP_DEVICE_JOYPAD 1

/** @brief Frames between two CRCs sent to the clients. */
#define NP_CRC_PERIOD 120

typedef enum {
    HC_FREE,
    HC_HEADER,
    HC_NICK,
    HC_INFO,
    HC_RUNNING
} hc_phase_t;

typedef struct {
    hc_phase_t phase;
    int        fd;
    uint32_t   client;       /* client number */
    char       nick[NP_NICK_LEN];
    uint32_t   compression;  /* agreed: 1 = zlib */
    time_t     since;        /* when the handshake started */
    bool       playing;
    bool       wants_state;  /* asked for a savestate */
    bool       gecnd;        /* this frontend: compares game CRCs */
    uint8_t   *in;  size_t in_len,  in_cap;
    uint8_t   *out; size_t out_len, out_cap;
} np_conn_t;

static struct {
    bool           active;
    int            listen_fd;
    netplay_core_t core;
    char           nick[NP_NICK_LEN];
    uint16_t       port;
    np_conn_t      conns[NP_HOST_CONNS];
    uint32_t       next_own;     /* next frame of ours to send */
    uint32_t       last_crc;     /* last frame a CRC was sent for */
    uint32_t       waited;       /* ticks too far ahead of a client's input */
    uint8_t       *zstate; size_t zstate_cap;
    bool           announce;     /* list the room in the lobby */
    char           game_name[128];
} h = { .listen_fd = -1 };


/* ------------------------------------------------------------------ */
/* Connections                                                         */
/* ------------------------------------------------------------------ */

static void hc_send_raw(np_conn_t *c, const void *data, size_t len) {
    if (c->fd < 0 || !reserve(&c->out, &c->out_cap, c->out_len + len)) return;
    memcpy(c->out + c->out_len, data, len);
    c->out_len += len;
}

static void hc_send_cmd(np_conn_t *c, uint32_t cmd, const void *payload, uint32_t size) {
    uint8_t head[8];
    wr32(head, cmd);
    wr32(head + 4, size);
    hc_send_raw(c, head, sizeof(head));
    if (size) hc_send_raw(c, payload, size);
}

/** @brief Sends a command to every client in the game but one. */
static void hc_broadcast(uint32_t cmd, const void *payload, uint32_t size, const np_conn_t *except) {
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase == HC_RUNNING && &h.conns[i] != except)
            hc_send_cmd(&h.conns[i], cmd, payload, size);
}

/** @brief MODE to one client: a player starts or stops playing at a frame. */
static void hc_send_mode_to(np_conn_t *to, uint32_t client, bool playing, uint32_t frame) {
    const np_player_t *p = &np_session.players[client];
    uint8_t m[NP_MODE_SIZE] = {0};

    wr32(m, frame);
    wr32(m + 4, (to->client == client ? NP_MODE_YOU : 0) | (playing ? NP_MODE_PLAYING : 0) | client);
    wr32(m + 8, playing ? p->devices : 0);
    memcpy(m + 12 + NP_MAX_DEVICES, p->nick, NP_NICK_LEN);
    hc_send_cmd(to, NP_CMD_MODE, m, sizeof(m));
}

/**
 * @brief MODE: a player starts or stops playing from a frame. The client
 * it is about gets it with the YOU bit.
 */
static void hc_send_mode(const np_conn_t *about, bool playing, uint32_t frame) {
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase == HC_RUNNING)
            hc_send_mode_to(&h.conns[i], about->client, playing, frame);
}

/** @brief Our input for a frame, read now, to every client. */
static void hc_send_own(uint32_t frame) {
    uint8_t p[8 + 4 * NP_MAX_WORDS];
    unsigned size;

    wr32(p, frame);
    wr32(p + 4, 0);
    size = np_local_input(0, &h.core, true, p + 8);
    np_store_input(0, frame, p + 8, size / 4);
    hc_broadcast(NP_CMD_INPUT, p, 8 + size, NULL);
}

/**
 * @brief A player stops at the next frame of our input stream: clients
 * (RetroArch's) take a MODE only at that frame. Frames before it the
 * player did not send count as idle, for everyone; if it sent past it,
 * our stream goes on to there first.
 */
static uint32_t hc_stop_playing(np_conn_t *c) {
    np_player_t *p = &np_session.players[c->client];
    const unsigned words = np_words_for(p->devices);
    uint32_t end = np_session.confirmed > p->from ? np_session.confirmed : p->from;
    uint8_t idle[8 + 4 * NP_MAX_WORDS] = {0};

    if (h.core.packets) {  /* no input stream */
        p->until = h.next_own;
        c->playing = false;
        return p->until;
    }
    while (np_input(c->client, end)) end++;
    while (h.next_own < end) hc_send_own(h.next_own++);
    wr32(idle + 4, c->client);
    for (uint32_t f = end; f < h.next_own; f++) {
        wr32(idle, f);
        np_store_input(c->client, f, idle + 8, words);  /* rewinds if guessed otherwise */
        hc_broadcast(NP_CMD_INPUT, idle, 8 + 4 * words, c);
    }
    p->until = h.next_own;
    c->playing = false;
    return p->until;
}

static void hc_drop(np_conn_t *c, const char *why) {
    if (c->phase == HC_FREE) return;
    fprintf(stderr, "[netplay] %s left: %s\n", c->nick[0] ? c->nick : "a client", why);
    if (h.core.packets && c->phase == HC_RUNNING && c->playing) {
        c->phase = HC_FREE;  /* gone before the core hears of it */
        c->playing = false;
        if (h.core.packets->disconnected) h.core.packets->disconnected((uint16_t)c->client);
    }
    if (c->phase == HC_RUNNING && c->playing) {
        /* nobody waits for it from the first frame it did not send */
        const uint32_t until = hc_stop_playing(c);
        c->phase = HC_FREE;  /* not told about its own leaving */
        hc_send_mode(c, false, until);
    }
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->phase = HC_FREE;
    c->in_len = c->out_len = 0;
}

static void hc_flush(np_conn_t *c) {
    while (c->fd >= 0 && c->out_len) {
        ssize_t n = send(c->fd, c->out, c->out_len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            hc_drop(c, "send failed");
            return;
        }
        memmove(c->out, c->out + n, c->out_len - (size_t)n);
        c->out_len -= (size_t)n;
    }
}

/* ------------------------------------------------------------------ */
/* Handshake                                                           */
/* ------------------------------------------------------------------ */

/** @brief The version peers compare: the core's netcode's, if it has one. */
static const char *hc_version(void) {
    if (h.core.packets && h.core.packets->protocol_version) return h.core.packets->protocol_version;
    return h.core.core_version ? h.core.core_version : "";
}

static void hc_send_info(np_conn_t *c) {
    uint8_t p[NP_INFO_SIZE] = {0};
    wr32(p, h.core.content_crc);
    snprintf((char *)p + 4, NP_NICK_LEN, "%s", h.core.core_name ? h.core.core_name : "");
    snprintf((char *)p + 4 + NP_NICK_LEN, NP_NICK_LEN, "%s", hc_version());
    hc_send_cmd(c, NP_CMD_INFO, p, sizeof(p));
}

/* ------------------------------------------------------------------ */
/* The core's packets                                                  */
/* ------------------------------------------------------------------ */

/** @brief A packet to one client: the word says who sent it. */
static void hc_send_packet(np_conn_t *c, uint32_t from, const void *buf, size_t len) {
    uint8_t head[12];
    wr32(head, NP_CMD_NETPACKET);
    wr32(head + 4, (uint32_t)len);  /* the packet only */
    wr32(head + 8, from);
    hc_send_raw(c, head, sizeof(head));
    if (len) hc_send_raw(c, buf, len);
}

static void hc_flush(np_conn_t *c);

/** @brief The core sends: to a client, or to every one. */
static void RETRO_CALLCONV hc_core_send(int flags, const void *buf, size_t len, uint16_t client_id) {
    if (!h.active) return;
    if (buf && len)
        for (unsigned i = 0; i < NP_HOST_CONNS; i++) {
            np_conn_t *c = &h.conns[i];
            if (c->phase != HC_RUNNING || !c->playing) continue;
            if (client_id == RETRO_NETPACKET_BROADCAST || c->client == client_id)
                hc_send_packet(c, 0, buf, len);
        }
    if (flags & RETRO_NETPACKET_FLUSH_HINT)
        for (unsigned i = 0; i < NP_HOST_CONNS; i++)
            if (h.conns[i].phase != HC_FREE) hc_flush(&h.conns[i]);
}

static void hc_io(np_conn_t *c);

/** @brief The core reads what arrived, without waiting for the frame. */
static void RETRO_CALLCONV hc_core_poll_receive(void) {
    for (unsigned i = 0; h.active && i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase != HC_FREE) hc_io(&h.conns[i]);
}

/**
 * @brief A client's packet: for us (delivered to the core), for another
 * client, or for everyone (both), passed on with who sent it.
 */
static void hc_on_packet(np_conn_t *c, const uint8_t *p, uint32_t size) {
    const uint32_t to = rd32(p);
    const uint8_t *data = p + 4;
    const size_t len = size - 4;

    if (to == 0 || to == NP_PACKET_BROADCAST)
        h.core.packets->receive(data, len, (uint16_t)c->client);
    for (unsigned i = 0; i < NP_HOST_CONNS; i++) {
        np_conn_t *o = &h.conns[i];
        if (o == c || o->phase != HC_RUNNING || !o->playing) continue;
        if (to == NP_PACKET_BROADCAST || o->client == to) hc_send_packet(o, c->client, data, len);
    }
}

/**
 * @brief The state at the start of the last final frame (no guess in
 * it), compressed when the client takes zlib.
 */
static void hc_send_state(np_conn_t *c) {
    const uint8_t *state, *data;
    size_t size, len;
    uint8_t *p;

    if (!np_rb_final_state(&state, &size)) {
        fprintf(stderr, "[netplay] the core cannot make a savestate for %s\n", c->nick);
        return;
    }
    data = state;
    len = size;
#ifdef GECND_NETPLAY_ZLIB
    if (c->compression) {
        uLongf zlen = compressBound(size);
        if (!reserve(&h.zstate, &h.zstate_cap, zlen) ||
            compress2(h.zstate, &zlen, state, size, Z_BEST_SPEED) != Z_OK) {
            fprintf(stderr, "[netplay] cannot compress a savestate for %s\n", c->nick);
            return;
        }
        data = h.zstate;
        len = zlen;
    }
#endif
    p = malloc(8 + len);
    if (!p) return;
    wr32(p, np_session.confirmed);  /* the next frame of our stream */
    wr32(p + 4, (uint32_t)size);
    memcpy(p + 8, data, len);
    hc_send_cmd(c, NP_CMD_LOAD_SAVESTATE, p, (uint32_t)(8 + len));
    free(p);
}

/**
 * @brief SYNC: where the session is (the last final frame), who controls
 * each port then, the client's nick and our SRAM.
 */
static void hc_send_sync(np_conn_t *c) {
    /* no SRAM for a core with its own netcode */
    const size_t sram = !h.core.packets && h.core.memory_size && h.core.memory_data &&
                        h.core.memory_data(0) ? h.core.memory_size(0) : 0;
    uint8_t *p = calloc(1, NP_SYNC_MIN + sram);
    uint8_t *q;

    if (!p) return;
    wr32(p, np_session.confirmed);
    wr32(p + 4, c->client);
    q = p + 8;
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++, q += 4) wr32(q, np_session.devices[d]);
    q += NP_MAX_DEVICES;  /* share modes: none */
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++, q += 4) {
        uint32_t clients = 0;
        for (unsigned k = 0; k < NP_MAX_CLIENTS; k++)
            if ((np_session.players[k].devices & (1u << d)) && np_player_at(k, np_session.confirmed))
                clients |= 1u << k;
        wr32(q, clients);
    }
    memcpy(q, c->nick, NP_NICK_LEN);
    q += NP_NICK_LEN;
    if (sram) memcpy(q, h.core.memory_data(0), sram);
    hc_send_cmd(c, NP_CMD_SYNC, p, (uint32_t)(NP_SYNC_MIN + sram));
    free(p);
}

/** @brief Closes a connection that never joined, quietly. */
static void hc_close(np_conn_t *c) {
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->phase = HC_FREE;
    c->in_len = c->out_len = 0;
}

/** @brief The client's header: answer ours, then our nick. */
static bool hc_on_header(np_conn_t *c, const uint8_t *in) {
    uint32_t hi, lo, protocol;
    uint8_t out[NP_HDR_WORDS * 4];

    if (rd32(in + 4 * NP_HDR_MAGIC) != NP_MAGIC_RANP) return false;
    lo = rd32(in + 4 * NP_HDR_PROTOCOL);
    hi = rd32(in + 4 * NP_HDR_SALT);     /* clients put their highest here */
    protocol = !hi ? lo : hi > NP_PROTOCOL_HIGH ? NP_PROTOCOL_HIGH : hi;
    if (protocol < NP_PROTOCOL_LOW || protocol > NP_PROTOCOL_HIGH) return false;
    c->compression = rd32(in + 4 * NP_HDR_COMPRESSION) & NP_COMPRESSION;
    c->gecnd = rd32(in + 4 * NP_HDR_IMPL) == NP_IMPL_TAG;

    wr32(out + 4 * NP_HDR_MAGIC,       NP_MAGIC_RANP);
    wr32(out + 4 * NP_HDR_PLATFORM,    np_platform());
    wr32(out + 4 * NP_HDR_COMPRESSION, NP_COMPRESSION);
    wr32(out + 4 * NP_HDR_SALT,        0);  /* no password */
    wr32(out + 4 * NP_HDR_PROTOCOL,    protocol);
    wr32(out + 4 * NP_HDR_IMPL,        NP_IMPL_TAG);
    hc_send_raw(c, out, sizeof(out));
    hc_send_cmd(c, NP_CMD_NICK, h.nick, NP_NICK_LEN);
    return true;
}

/* ------------------------------------------------------------------ */
/* Commands of a client in the game                                    */
/* ------------------------------------------------------------------ */

/** @brief PLAY: the first free port, from the next frame. */
static void hc_on_play(np_conn_t *c) {
    np_player_t *p = &np_session.players[c->client];
    uint8_t refused[4];

    if (c->playing) return;
    if (h.core.packets) {
        /* the core's own netcode: no port, the core takes it or not;
         * it may send to the client already while deciding */
        c->playing = true;
        if (h.core.packets->connected && !h.core.packets->connected((uint16_t)c->client)) {
            c->playing = false;
            wr32(refused, NP_REFUSED_OTHER);
            hc_send_cmd(c, NP_CMD_MODE_REFUSED, refused, sizeof(refused));
            return;
        }
        p->devices = 0;
        p->from = h.next_own;
        p->until = UINT32_MAX;
        memcpy(p->nick, c->nick, NP_NICK_LEN);
        fprintf(stderr, "[netplay] %s plays (client %u)\n", c->nick, c->client);
        hc_send_mode(c, true, p->from);
        return;
    }
    for (unsigned d = 0; d < NP_HOST_PORTS; d++) {
        bool taken = false;
        for (unsigned k = 0; k < NP_MAX_CLIENTS; k++)
            if ((np_session.players[k].devices & (1u << d)) &&
                np_session.players[k].until == UINT32_MAX)
                taken = true;
        if (taken) continue;

        p->devices = 1u << d;
        p->from = h.next_own;  /* the next frame of our input stream */
        p->until = UINT32_MAX;
        memcpy(p->nick, c->nick, NP_NICK_LEN);
        c->playing = true;
        fprintf(stderr, "[netplay] %s plays on port %u from frame %u\n", c->nick, d + 1, p->from);
        hc_send_mode(c, true, p->from);
        return;
    }
    wr32(refused, NP_REFUSED_NO_SLOTS);
    hc_send_cmd(c, NP_CMD_MODE_REFUSED, refused, sizeof(refused));
}

/** @brief SPECTATE: stops counting after what it already sent. */
static void hc_on_spectate(np_conn_t *c) {
    if (!c->playing) return;
    hc_send_mode(c, false, hc_stop_playing(c));
}

static void hc_on_command(np_conn_t *c, uint32_t cmd, const uint8_t *p, uint32_t size) {
    switch (cmd) {
    case NP_CMD_NETPACKET:
        /* taken even before PLAY: a client's core starts right after
         * SYNC and may speak first */
        if (size >= 4 && h.core.packets) hc_on_packet(c, p, size);
        break;
    case NP_CMD_INPUT:
        if (size >= 8 && c->playing && !h.core.packets) {
            const uint32_t frame = rd32(p);
            const np_player_t *pl = &np_session.players[c->client];
            unsigned words = np_words_for(pl->devices);
            uint8_t fwd[8 + 4 * NP_MAX_WORDS];

            if (words > (size - 8) / 4) words = (size - 8) / 4;
            if (words > NP_MAX_WORDS) words = NP_MAX_WORDS;
            /* frames still open, and the client number the connection's,
             * whatever it wrote; then on to everyone else at once */
            if (frame < np_session.confirmed || frame - np_session.confirmed >= NP_RING / 2 ||
                frame < pl->from || frame >= pl->until || np_input(c->client, frame))
                break;
            np_store_input(c->client, frame, p + 8, words);
            wr32(fwd, frame);
            wr32(fwd + 4, c->client);
            memcpy(fwd + 8, p + 8, 4 * words);
            hc_broadcast(NP_CMD_INPUT, fwd, 8 + 4 * words, c);
        }
        break;
    case NP_CMD_PLAY:
        hc_on_play(c);
        break;
    case NP_CMD_SPECTATE:
        hc_on_spectate(c);
        break;
    case NP_CMD_REQUEST_SAVESTATE:
        c->wants_state = true;
        break;
    case NP_CMD_PING_REQUEST:
        hc_send_cmd(c, NP_CMD_PING_RESPONSE, NULL, 0);
        break;
    case NP_CMD_DISCONNECT:
        hc_drop(c, "disconnected");
        break;
    case NP_CMD_NAK:
        hc_drop(c, "refused a command");
        break;
    default:  /* CRC, chat, settings: nothing to do */
        break;
    }
}

/**
 * @brief A client joining at the last final frame: who started or stopped
 * playing since, and every real input since, ours included.
 */
static void hc_catch_up(np_conn_t *c) {
    const uint32_t from = np_session.confirmed;
    uint8_t p[8 + 4 * NP_MAX_WORDS];

    /* in stream order: a MODE goes right before the inputs of its frame */
    for (uint32_t f = from; f <= h.next_own; f++) {
        for (uint32_t k = 0; k < NP_MAX_CLIENTS; k++) {
            const np_player_t *pl = &np_session.players[k];
            if (!pl->devices || k == c->client) continue;
            if (pl->from == f && f > from) hc_send_mode_to(c, k, true, f);
            if (pl->until == f && f > from) hc_send_mode_to(c, k, false, f);
        }
        if (f == h.next_own) break;
        for (uint32_t k = 0; k < NP_MAX_CLIENTS; k++) {
            const np_input_t *in = np_input(k, f);
            const unsigned words = np_words_for(np_session.players[k].devices);
            if (!in || !np_player_at(k, f)) continue;
            wr32(p, f);
            wr32(p + 4, k);
            for (unsigned w = 0; w < words && w < NP_MAX_WORDS; w++) wr32(p + 8 + 4 * w, in->words[w]);
            hc_send_cmd(c, NP_CMD_INPUT, p, 8 + 4 * words);
        }
    }
}

/** @brief Consumes what a client sent: the header, then whole commands. */
static void hc_parse(np_conn_t *c) {
    size_t used = 0;

    if (c->phase == HC_HEADER) {
        /* the lobby checking the room: answer our header and close */
        if (c->in_len >= 4 && rd32(c->in) == NP_MAGIC_POKE) {
            uint8_t out[NP_HDR_WORDS * 4];
            wr32(out + 4 * NP_HDR_MAGIC,       NP_MAGIC_RANP);
            wr32(out + 4 * NP_HDR_PLATFORM,    np_platform());
            wr32(out + 4 * NP_HDR_COMPRESSION, NP_COMPRESSION);
            wr32(out + 4 * NP_HDR_SALT,        0);
            wr32(out + 4 * NP_HDR_PROTOCOL,    NP_PROTOCOL_HIGH);
            wr32(out + 4 * NP_HDR_IMPL,        NP_IMPL_TAG);
            send(c->fd, out, sizeof(out), MSG_NOSIGNAL);
            hc_close(c);
            return;
        }
        if (c->in_len < NP_HDR_WORDS * 4) return;
        if (!hc_on_header(c, c->in)) {
            hc_drop(c, "not a netplay client of a protocol we speak");
            return;
        }
        used = NP_HDR_WORDS * 4;
        c->phase = HC_NICK;
    }

    while (c->fd >= 0 && c->in_len - used >= 8) {
        const uint32_t cmd  = rd32(c->in + used);
        const uint32_t size = np_payload_size(cmd, rd32(c->in + used + 4));
        const uint8_t *p    = c->in + used + 8;

        if (size > 1024u * 1024) {
            hc_drop(c, "command too big");
            return;
        }
        if (c->in_len - used - 8 < size) break;
        used += 8 + size;

        switch (c->phase) {
        case HC_NICK:
            if (cmd != NP_CMD_NICK || size < NP_NICK_LEN) { hc_drop(c, "expected NICK"); return; }
            memcpy(c->nick, p, NP_NICK_LEN);
            c->nick[NP_NICK_LEN - 1] = '\0';
            hc_send_info(c);
            c->phase = HC_INFO;
            break;
        case HC_INFO:
            if (cmd != NP_CMD_INFO || size < NP_INFO_SIZE) { hc_drop(c, "expected INFO"); return; }
            {
                char name[NP_NICK_LEN + 1] = {0};
                memcpy(name, p + 4, NP_NICK_LEN);
                if (strcasecmp(name, h.core.core_name ? h.core.core_name : "")) {
                    hc_drop(c, "runs another core");
                    return;
                }
                if (rd32(p) != h.core.content_crc)
                    fprintf(stderr, "[netplay] %s has other content (crc %08x, ours %08x)\n",
                            c->nick, rd32(p), h.core.content_crc);
            }
            hc_send_sync(c);
            if (h.core.packets) {
                /* the core's own netcode: no state; the core hears of
                 * the client once it asks to play */
                c->phase = HC_RUNNING;
                fprintf(stderr, "[netplay] %s joined as client %u\n", c->nick, c->client);
                break;
            }
            hc_send_state(c);
            c->phase = HC_RUNNING;
            hc_catch_up(c);
            fprintf(stderr, "[netplay] %s joined at frame %u\n", c->nick, np_session.confirmed);
            break;
        case HC_RUNNING:
            hc_on_command(c, cmd, p, size);
            break;
        default:
            break;
        }
    }

    if (c->fd >= 0 && used) {
        memmove(c->in, c->in + used, c->in_len - used);
        c->in_len -= used;
    }
}

static void hc_io(np_conn_t *c) {
    for (;;) {
        ssize_t n;
        if (!reserve(&c->in, &c->in_cap, c->in_len + 65536)) {
            hc_drop(c, "out of memory");
            return;
        }
        n = recv(c->fd, c->in + c->in_len, c->in_cap - c->in_len, 0);
        if (n > 0) {
            c->in_len += (size_t)n;
            continue;
        }
        if (n == 0) {
            hc_drop(c, "closed the connection");
            return;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            hc_drop(c, strerror(errno));
            return;
        }
        break;
    }
    hc_parse(c);
    if (c->phase != HC_FREE && c->phase != HC_RUNNING &&
        time(NULL) - c->since > NP_HANDSHAKE_TIMEOUT)
        hc_drop(c, "handshake timed out");
}

/**
 * @brief Takes a client's connection (straight or through the relay) into
 * a free slot; a full host says so and closes it.
 */
static void hc_adopt(int fd) {
    np_conn_t *c = NULL;
    int one = 1;

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase == HC_FREE) { c = &h.conns[i]; break; }
    if (!c) {
        uint8_t full[NP_HDR_WORDS * 4] = {0};
        wr32(full, NP_MAGIC_FULL);
        wr32(full + 4 * NP_HDR_PROTOCOL, NP_PROTOCOL_HIGH);
        send(fd, full, sizeof(full), MSG_NOSIGNAL);
        close(fd);
        return;
    }
    c->fd = fd;
    c->phase = HC_HEADER;
    c->since = time(NULL);
    c->nick[0] = '\0';
    c->playing = c->wants_state = c->gecnd = false;
    c->in_len = c->out_len = 0;
    c->client = (uint32_t)(c - h.conns) + 1;
    memset(&np_session.players[c->client], 0, sizeof(np_player_t));
}

/** @brief Takes new straight connections. */
static void hc_accept(void) {
    for (;;) {
        int fd = accept(h.listen_fd, NULL, NULL);
        if (fd < 0) return;
        hc_adopt(fd);
    }
}

/* ------------------------------------------------------------------ */
/* Session                                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief A frame became final: every NP_CRC_PERIOD frames its CRC goes
 * to the clients, of the game to this frontend's, of the whole state to
 * RetroArch's when that means something for the core.
 */
static void hc_on_final(uint32_t frame, uint32_t game_crc) {
    uint8_t p[8];
    uint32_t state_crc = 0;
    bool has_state_crc = false;

    if (frame % NP_CRC_PERIOD || frame <= h.last_crc) return;
    h.last_crc = frame;
    for (unsigned i = 0; i < NP_HOST_CONNS; i++) {
        np_conn_t *c = &h.conns[i];
        if (c->phase != HC_RUNNING) continue;
        wr32(p, frame);
        if (c->gecnd) {
            wr32(p + 4, game_crc);
        } else {
            if (!has_state_crc && (!np_rb_states_portable() || !np_rb_state_crc(frame, &state_crc)))
                continue;
            has_state_crc = true;
            wr32(p + 4, state_crc);
        }
        hc_send_cmd(c, NP_CMD_CRC, p, sizeof(p));
    }
}

bool netplay_host_start(uint16_t port, const char *nick, const netplay_core_t *core) {
    struct sockaddr_in addr = {0};
    int fd, one = 1;

    netplay_host_stop();
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 8) < 0) {
        fprintf(stderr, "[netplay] cannot host on port %u: %s\n", port, strerror(errno));
        close(fd);
        return false;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    np_session_reset();
    memset(h.conns, 0, sizeof(h.conns));
    for (unsigned i = 0; i < NP_HOST_CONNS; i++) h.conns[i].fd = -1;
    h.listen_fd = fd;
    h.port = port;
    h.core = *core;
    snprintf(h.nick, sizeof(h.nick), "%s", nick && nick[0] ? nick : "gecnd");
    h.next_own = 0;
    h.last_crc = 0;
    h.waited = 0;

    /* every port a joypad, on our core as on every client's */
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++) {
        np_session.devices[d] = NP_DEVICE_JOYPAD;
        if (h.core.set_port_device) h.core.set_port_device(d, NP_DEVICE_JOYPAD);
    }
    /* the host plays port 1 */
    np_session.players[0].devices = 1u << 0;
    np_session.players[0].from = 0;
    np_session.players[0].until = UINT32_MAX;
    memcpy(np_session.players[0].nick, h.nick, NP_NICK_LEN);

    if (!h.core.packets && !np_rb_start(&h.core, 0, hc_on_final)) {
        close(fd);
        h.listen_fd = -1;
        return false;
    }

    h.announce = false;
    netplay_relay_stop();

    h.active = true;
    fprintf(stderr, "[netplay] hosting on port %u as %s%s\n", port, h.nick,
            h.core.packets ? " (the core's own netcode)" : "");
    if (h.core.packets) h.core.packets->start(0, hc_core_send, hc_core_poll_receive);
    return true;
}

void netplay_host_stop(void) {
    if (!h.active) return;
    for (unsigned i = 0; i < NP_HOST_CONNS; i++) {
        np_conn_t *c = &h.conns[i];
        if (c->phase == HC_RUNNING) {
            hc_send_cmd(c, NP_CMD_DISCONNECT, NULL, 0);
            hc_flush(c);
        }
        if (c->fd >= 0) close(c->fd);
        c->fd = -1;
        c->phase = HC_FREE;
        free(c->in);
        free(c->out);
        c->in = c->out = NULL;
        c->in_cap = c->out_cap = c->in_len = c->out_len = 0;
    }
    if (h.listen_fd >= 0) close(h.listen_fd);
    h.listen_fd = -1;
    h.active = false;
    np_rb_stop();
    if (h.core.packets && h.core.packets->stop) h.core.packets->stop();
    netplay_lobby_stop();
    netplay_relay_stop();
}

/** @brief Lists the room, through the relay when there is one. */
static void hc_lobby_start(void) {
    netplay_lobby_room_t room = {
        .nick         = h.nick,
        .core_name    = h.core.core_name,
        .core_version = h.core.core_version,
        .game_name    = h.game_name,
        .game_crc     = h.core.content_crc,
        .port         = h.port,
        .mitm_server  = netplay_relay_session() ? netplay_relay_handle() : NULL,
        .mitm_session = netplay_relay_session(),
    };
    netplay_lobby_start(&room);
}

void netplay_host_announce(const char *game_name) {
    if (!h.active) return;
    h.announce = true;
    snprintf(h.game_name, sizeof(h.game_name), "%s", game_name ? game_name : "");
    /* with a relay coming, wait for its session to list it */
    if (!netplay_relay_wanted() || netplay_relay_session()) hc_lobby_start();
}

static void hc_on_relay_link(int fd, void *user) {
    (void)user;
    hc_adopt(fd);
}

static void hc_on_relay_session(const char *handle, const char *session, void *user) {
    (void)handle; (void)session; (void)user;
    if (h.announce) hc_lobby_start();
}

bool netplay_host_relay(const char *handle) {
    return h.active && netplay_relay_host(handle, hc_on_relay_link, hc_on_relay_session, NULL);
}

bool netplay_host_active(void) {
    return h.active;
}

/** @brief Whether a running client asked for a savestate. */
static bool hc_state_wanted(void) {
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase == HC_RUNNING && h.conns[i].wants_state) return true;
    return false;
}

/**
 * @brief Savestates asked for. A state goes in our input stream at its
 * next frame (clients take it only there), and only once every frame
 * before is final: until then our stream stops (hc_run_frame).
 */
static void hc_send_states(void) {
    if (np_session.confirmed != h.next_own || np_session.self_frame != h.next_own) return;
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase == HC_RUNNING && h.conns[i].wants_state) {
            h.conns[i].wants_state = false;
            hc_send_state(&h.conns[i]);
        }
}

/**
 * @brief Runs the next frame, unless that would get too far past the
 * last final one (a client's input is late), or a savestate is waiting
 * for the frames already in our stream to be final: then it waits.
 */
static void hc_run_frame(void) {
    const uint32_t frame = np_session.self_frame;

    if (!np_rb_can_run()) {
        h.waited++;
        return;
    }
    if (!hc_state_wanted())
        while (h.next_own <= frame + np_input_delay) hc_send_own(h.next_own++);
    if (h.next_own <= frame) {
        h.waited++;
        return;
    }
    np_rb_run();

    if (np_session.self_frame % 600 == 0) {
        unsigned players = 0, rollbacks, replayed;
        for (unsigned k = 0; k < NP_MAX_CLIENTS; k++) players += np_player_at(k, np_session.self_frame);
        np_rb_stats(&rollbacks, &replayed);
        fprintf(stderr, "[netplay] frame %u, final %u, %u players, %u rollbacks "
                "(%u frames run again), %u ticks waiting for input\n",
                np_session.self_frame, np_session.confirmed, players, rollbacks, replayed, h.waited);
        h.waited = 0;
    }
}

void netplay_host_tick(void) {
    if (!h.active) return;
    hc_accept();
    netplay_relay_tick();
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase != HC_FREE) hc_io(&h.conns[i]);

    if (h.core.packets) {
        /* the core's own netcode: a frame as without netplay */
        if (h.core.packets->poll) h.core.packets->poll();
        h.core.run();
    } else {
        /* wrong guesses first, then what became final */
        np_rb_resolve();
        np_rb_confirm(NP_NO_FRAME);
        hc_send_states();
        hc_run_frame();
    }
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase != HC_FREE) hc_flush(&h.conns[i]);
    {
        unsigned players = 1, spectators = 0;  /* the host plays */
        for (unsigned i = 0; i < NP_HOST_CONNS; i++)
            if (h.conns[i].phase == HC_RUNNING) {
                if (h.conns[i].playing) players++;
                else spectators++;
            }
        netplay_lobby_tick(players, spectators);
    }
}

#endif
