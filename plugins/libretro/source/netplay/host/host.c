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
 * Every frame: once every player's input for it is here, the host
 * forwards each client's input to the others, sends its own (the frame's
 * synchronization point) and runs the core. A client that leaves stops
 * being waited for from the first frame it did not send.
 */
#include "netplay/host/host.h"

#ifdef _WIN32

/* TODO: Winsock; until then netplay is off on Windows */
bool netplay_host_start(uint16_t port, const char *nick, const netplay_core_t *core) {
    (void)port; (void)nick; (void)core;
    return false;
}
void netplay_host_announce(const char *game_name) { (void)game_name; }
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

#include "netplay/lobby/lobby.h"

#define rd32    np_rd32
#define wr32    np_wr32
#define reserve np_reserve

/** @brief Connections at once; client numbers are 1..NP_HOST_CONNS. */
#define NP_HOST_CONNS 8

/** @brief Ports given to players, the host's (0) included. */
#define NP_HOST_PORTS 4

/** @brief Seconds a client may take to finish the handshake. */
#define NP_HANDSHAKE_TIMEOUT 15

/** @brief MODE_REFUSED reason: every port is taken. */
#define NP_REFUSED_NO_SLOTS 2

/** @brief RETRO_DEVICE_JOYPAD, what every port holds. */
#define NP_DEVICE_JOYPAD 1

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
    uint32_t       self_frame;   /* next frame to run */
    uint32_t       waited;       /* ticks spent waiting for a client's input */
    uint8_t       *state;  size_t state_cap;
    uint8_t       *zstate; size_t zstate_cap;
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

/**
 * @brief MODE: a player starts or stops playing from a frame. The client
 * it is about gets it with the YOU bit.
 */
static void hc_send_mode(const np_conn_t *about, bool playing, uint32_t frame) {
    const np_player_t *p = &np_session.players[about->client];
    uint8_t m[NP_MODE_SIZE] = {0};

    wr32(m, frame);
    wr32(m + 8, playing ? p->devices : 0);
    memcpy(m + 12 + NP_MAX_DEVICES, about->nick, NP_NICK_LEN);
    for (unsigned i = 0; i < NP_HOST_CONNS; i++) {
        np_conn_t *c = &h.conns[i];
        if (c->phase != HC_RUNNING) continue;
        wr32(m + 4, (c == about ? NP_MODE_YOU : 0) | (playing ? NP_MODE_PLAYING : 0) | about->client);
        hc_send_cmd(c, NP_CMD_MODE, m, sizeof(m));
    }
}

static void hc_drop(np_conn_t *c, const char *why) {
    if (c->phase == HC_FREE) return;
    fprintf(stderr, "[netplay] %s left: %s\n", c->nick[0] ? c->nick : "a client", why);
    if (c->phase == HC_RUNNING && c->playing) {
        /* nobody waits for it from the first frame it did not send */
        np_player_t *p = &np_session.players[c->client];
        uint32_t until = p->from;
        while (until < h.self_frame + NP_RING && np_input(c->client, until)) until++;
        if (until < h.self_frame) until = h.self_frame;
        p->until = until;
        c->playing = false;
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

static void hc_send_info(np_conn_t *c) {
    uint8_t p[NP_INFO_SIZE] = {0};
    wr32(p, h.core.content_crc);
    snprintf((char *)p + 4, NP_NICK_LEN, "%s", h.core.core_name ? h.core.core_name : "");
    snprintf((char *)p + 4 + NP_NICK_LEN, NP_NICK_LEN, "%s",
             h.core.core_version ? h.core.core_version : "");
    hc_send_cmd(c, NP_CMD_INFO, p, sizeof(p));
}

/**
 * @brief Our savestate as the start of the next frame, compressed when
 * the client takes zlib.
 */
static void hc_send_state(np_conn_t *c) {
    const size_t size = h.core.serialize_size ? h.core.serialize_size() : 0;
    const uint8_t *data;
    size_t len;
    uint8_t *p;

    if (!size || !h.core.serialize || !reserve(&h.state, &h.state_cap, size) ||
        !h.core.serialize(h.state, size)) {
        fprintf(stderr, "[netplay] the core cannot make a savestate for %s\n", c->nick);
        return;
    }
    data = h.state;
    len = size;
#ifdef GECND_NETPLAY_ZLIB
    if (c->compression) {
        uLongf zlen = compressBound(size);
        if (!reserve(&h.zstate, &h.zstate_cap, zlen) ||
            compress2(h.zstate, &zlen, h.state, size, Z_BEST_SPEED) != Z_OK) {
            fprintf(stderr, "[netplay] cannot compress a savestate for %s\n", c->nick);
            return;
        }
        data = h.zstate;
        len = zlen;
    }
#endif
    p = malloc(8 + len);
    if (!p) return;
    wr32(p, h.self_frame);
    wr32(p + 4, (uint32_t)size);
    memcpy(p + 8, data, len);
    hc_send_cmd(c, NP_CMD_LOAD_SAVESTATE, p, (uint32_t)(8 + len));
    free(p);
}

/**
 * @brief SYNC: where the session is, who controls each port, the
 * client's nick and our SRAM.
 */
static void hc_send_sync(np_conn_t *c) {
    const size_t sram = h.core.memory_size && h.core.memory_data && h.core.memory_data(0) ?
                        h.core.memory_size(0) : 0;
    uint8_t *p = calloc(1, NP_SYNC_MIN + sram);
    uint8_t *q;

    if (!p) return;
    wr32(p, h.self_frame);
    wr32(p + 4, c->client);
    q = p + 8;
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++, q += 4) wr32(q, np_session.devices[d]);
    q += NP_MAX_DEVICES;  /* share modes: none */
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++, q += 4) {
        uint32_t clients = 0;
        for (unsigned k = 0; k < NP_MAX_CLIENTS; k++)
            if ((np_session.players[k].devices & (1u << d)) && np_player_at(k, h.self_frame))
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
    for (unsigned d = 0; d < NP_HOST_PORTS; d++) {
        bool taken = false;
        for (unsigned k = 0; k < NP_MAX_CLIENTS; k++)
            if ((np_session.players[k].devices & (1u << d)) &&
                np_session.players[k].until == UINT32_MAX)
                taken = true;
        if (taken) continue;

        p->devices = 1u << d;
        p->from = h.self_frame;
        p->until = UINT32_MAX;
        memcpy(p->nick, c->nick, NP_NICK_LEN);
        c->playing = true;
        fprintf(stderr, "[netplay] %s plays on port %u from frame %u\n", c->nick, d + 1, h.self_frame);
        hc_send_mode(c, true, h.self_frame);
        return;
    }
    wr32(refused, NP_REFUSED_NO_SLOTS);
    hc_send_cmd(c, NP_CMD_MODE_REFUSED, refused, sizeof(refused));
}

/** @brief SPECTATE: stops being waited for after what it already sent. */
static void hc_on_spectate(np_conn_t *c) {
    np_player_t *p = &np_session.players[c->client];
    uint32_t until = h.self_frame;

    if (!c->playing) return;
    while (until < h.self_frame + NP_RING && np_input(c->client, until)) until++;
    p->until = until;
    c->playing = false;
    hc_send_mode(c, false, until);
}

static void hc_on_command(np_conn_t *c, uint32_t cmd, const uint8_t *p, uint32_t size) {
    switch (cmd) {
    case NP_CMD_INPUT:
        if (size >= 8 && c->playing) {
            const uint32_t frame = rd32(p);
            const unsigned words = np_words_for(np_session.players[c->client].devices);
            /* the client number is the connection's, whatever it wrote */
            if (frame >= h.self_frame && frame < h.self_frame + NP_RING &&
                frame >= np_session.players[c->client].from)
                np_store_input(c->client, frame, p + 8,
                               words < (size - 8) / 4 ? words : (size - 8) / 4);
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
        const uint32_t size = rd32(c->in + used + 4);
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
            hc_send_state(c);
            c->phase = HC_RUNNING;
            fprintf(stderr, "[netplay] %s joined at frame %u\n", c->nick, h.self_frame);
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

/** @brief Takes new connections; a full host says so and closes. */
static void hc_accept(void) {
    for (;;) {
        int fd = accept(h.listen_fd, NULL, NULL);
        np_conn_t *c = NULL;

        if (fd < 0) return;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        {
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        }
        for (unsigned i = 0; i < NP_HOST_CONNS; i++)
            if (h.conns[i].phase == HC_FREE) { c = &h.conns[i]; break; }
        if (!c) {
            uint8_t full[NP_HDR_WORDS * 4] = {0};
            wr32(full, NP_MAGIC_FULL);
            wr32(full + 4 * NP_HDR_PROTOCOL, NP_PROTOCOL_HIGH);
            send(fd, full, sizeof(full), MSG_NOSIGNAL);
            close(fd);
            continue;
        }
        c->fd = fd;
        c->phase = HC_HEADER;
        c->since = time(NULL);
        c->nick[0] = '\0';
        c->playing = c->wants_state = false;
        c->in_len = c->out_len = 0;
        c->client = (uint32_t)(c - h.conns) + 1;
        memset(&np_session.players[c->client], 0, sizeof(np_player_t));
    }
}

/* ------------------------------------------------------------------ */
/* Session                                                             */
/* ------------------------------------------------------------------ */

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
    h.self_frame = 0;
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

    h.active = true;
    fprintf(stderr, "[netplay] hosting on port %u as %s\n", port, h.nick);
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
    netplay_lobby_stop();
}

void netplay_host_announce(const char *game_name) {
    netplay_lobby_room_t room = {
        .nick         = h.nick,
        .core_name    = h.core.core_name,
        .core_version = h.core.core_version,
        .game_name    = game_name,
        .game_crc     = h.core.content_crc,
        .port         = h.port,
    };
    if (h.active) netplay_lobby_start(&room);
}

bool netplay_host_active(void) {
    return h.active;
}

/**
 * @brief Runs the next frame if every player's input for it is here:
 * savestates asked for, the clients' inputs to everyone else, our input
 * (the frame's synchronization point), then the core.
 */
static void hc_run_frame(void) {
    const uint32_t frame = h.self_frame;
    uint8_t p[8 + 4 * NP_MAX_WORDS];
    unsigned size;

    if (!np_inputs_ready(frame, 0)) {  /* waiting for someone */
        h.waited++;
        return;
    }

    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase == HC_RUNNING && h.conns[i].wants_state) {
            h.conns[i].wants_state = false;
            hc_send_state(&h.conns[i]);
        }

    for (unsigned i = 0; i < NP_HOST_CONNS; i++) {
        const np_conn_t *c = &h.conns[i];
        const np_input_t *in;
        if (!np_player_at(c->client, frame) || !(in = np_input(c->client, frame))) continue;
        wr32(p, frame);
        wr32(p + 4, c->client);
        size = 4 * np_words_for(np_session.players[c->client].devices);
        for (unsigned w = 0; w < size / 4; w++) wr32(p + 8 + 4 * w, in->words[w]);
        hc_broadcast(NP_CMD_INPUT, p, 8 + size, c);
    }

    wr32(p, frame);
    wr32(p + 4, 0);
    size = np_local_input(0, &h.core, true, p + 8);
    np_store_input(0, frame, p + 8, size / 4);
    hc_broadcast(NP_CMD_INPUT, p, 8 + size, NULL);

    np_session.run_frame = frame;
    h.core.run();
    h.self_frame++;
    if (h.self_frame % 600 == 0) {
        unsigned players = 0;
        for (unsigned k = 0; k < NP_MAX_CLIENTS; k++) players += np_player_at(k, h.self_frame);
        fprintf(stderr, "[netplay] frame %u, %u players, %u ticks waiting for input\n",
                h.self_frame, players, h.waited);
        h.waited = 0;
    }
}

void netplay_host_tick(void) {
    if (!h.active) return;
    hc_accept();
    for (unsigned i = 0; i < NP_HOST_CONNS; i++)
        if (h.conns[i].phase != HC_FREE) hc_io(&h.conns[i]);
    hc_run_frame();
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
