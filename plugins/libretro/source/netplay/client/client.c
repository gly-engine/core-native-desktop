/**
 * @file plugins/libretro/source/netplay/client/client.c
 * @brief Netplay client for RetroArch compatible hosts (protocol.h).
 *
 * Flow: connect, exchange the connection header, NICK, receive the host's
 * INFO and answer with ours, receive SYNC (frame, devices, who controls
 * them, SRAM), then the host's savestate. From there every frame needs
 * the host's INPUT or NOINPUT for it (the host's clock) and the input of
 * every other player in the game; once all is there the frame runs with
 * exactly those inputs, ours included, so the core stays in sync without
 * ever predicting. A PLAY request turns this client into a player: the
 * host answers with MODE, and from its frame on this client sends its
 * input for every frame it runs.
 */
#include "netplay/client/client.h"
#include "netplay/common/protocol.h"
#include "netplay/lobby/relay.h"

#ifdef _WIN32

/* TODO: Winsock; until then netplay is off on Windows */
bool netplay_client_start(const char *host, uint16_t port, const char *mitm_session,
                          const char *nick, const netplay_core_t *core) {
    (void)host; (void)port; (void)mitm_session; (void)nick; (void)core;
    return false;
}
void netplay_client_stop(void) {}
bool netplay_client_active(void) { return false; }
void netplay_client_tick(void) {}
int16_t netplay_client_input(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)port; (void)device; (void)index; (void)id;
    return 0;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifdef GECND_NETPLAY_ZLIB
#include <zlib.h>
#endif

#define rd32    np_rd32
#define wr32    np_wr32
#define reserve np_reserve

/** @brief Frames run in one tick at most, to catch up with the host. */
#define NP_MAX_CATCHUP 4

/**
 * @brief Frames ahead of the one running that our input is sent for. A
 * host that predicts (RetroArch) would not need it, but one that waits for
 * every input before running a frame (another gecnd) would wait for us
 * while we wait for it; sending ahead breaks that, at the cost of this
 * many frames of input delay for us.
 */
#define NP_INPUT_LEAD 4

/** @brief Seconds without a byte from the host before giving up. */
#define NP_TIMEOUT 15

/** @brief CRC checks from the host waiting for their frame. */
#define NP_CRCS 8

/** @brief Our own state CRCs kept to answer the host's late checks: one
 * every NP_HISTORY_STEP frames (the host's default check is every 600). */
#define NP_HISTORY      32
#define NP_HISTORY_STEP 60

typedef enum {
    NP_OFF,
    NP_CONNECTING,
    NP_HEADER,
    NP_NICK,
    NP_INFO,
    NP_SYNC,
    NP_RUNNING
} np_phase_t;

static struct {
    np_phase_t     phase;
    int            fd;
    netplay_core_t core;
    char           nick[NP_NICK_LEN];
    time_t         last_recv;

    uint8_t *in;  size_t in_len,  in_cap;
    uint8_t *out; size_t out_len, out_cap;

    uint32_t protocol;
    uint32_t compression;   /* agreed with the host: 1 = zlib */
    uint32_t client;        /* our client number */

    uint32_t self_frame;    /* next frame to run */
    uint32_t server_frame;  /* every frame before it has the host's word */
    bool     have_state;
    bool     paused;
    uint32_t stall;
    bool     reset_pending;
    uint32_t reset_frame;

    struct { uint32_t frame, hash; bool set; } crcs[NP_CRCS];
    struct { uint32_t frame, hash; bool set; } history[NP_HISTORY];
    unsigned crc_ok, crc_bad;
    bool     crc_usable;     /* the core's states are byte stable */
    bool     crc_known;      /* crc_usable was measured */
    uint8_t *pending;        /* a host savestate for a frame still ahead */
    uint32_t pending_frame;
    size_t   pending_size;
    unsigned stable_bytes;   /* bytes a round trip changes (pointers) */
    uint8_t *unstable;       /* 1 where a round trip changes the byte */
    uint8_t *state;          /* scratch for CRC checks */
    size_t   state_cap;

    bool     play_asked;
    bool     playing;
    uint32_t next_send;     /* next frame of ours to send */
} np = { .fd = -1 };

/* ------------------------------------------------------------------ */
/* Connection                                                          */
/* ------------------------------------------------------------------ */

static void np_close(void) {
    free(np.pending);
    np.pending = NULL;
    if (np.fd >= 0) close(np.fd);
    np.fd = -1;
    np.phase = NP_OFF;
    np.in_len = np.out_len = 0;
}

static void np_fail(const char *why) {
    fprintf(stderr, "[netplay] %s, leaving the session\n", why);
    np_close();
}

static void np_flush(void) {
    while (np.fd >= 0 && np.out_len) {
        ssize_t n = send(np.fd, np.out, np.out_len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            np_fail("send failed");
            return;
        }
        memmove(np.out, np.out + n, np.out_len - (size_t)n);
        np.out_len -= (size_t)n;
    }
}

static void np_send_raw(const void *data, size_t len) {
    if (np.fd < 0 || !reserve(&np.out, &np.out_cap, np.out_len + len)) return;
    memcpy(np.out + np.out_len, data, len);
    np.out_len += len;
}

static void np_send_cmd(uint32_t cmd, const void *payload, uint32_t size) {
    uint8_t head[8];
    wr32(head, cmd);
    wr32(head + 4, size);
    np_send_raw(head, sizeof(head));
    if (size) np_send_raw(payload, size);
}

/** @brief The relay session to ask for before speaking netplay, if any. */
static struct {
    bool    on;
    uint8_t id[NP_RELAY_ID_SIZE];
} np_relay;

static void np_send_header(void) {
    uint8_t h[NP_HDR_WORDS * 4];
    wr32(h + 4 * NP_HDR_MAGIC,       NP_MAGIC_RANP);
    wr32(h + 4 * NP_HDR_PLATFORM,    np_platform());
    /* RetroArch hosts send savestates zlib compressed, and do not cope
     * well with a client that takes them raw */
    wr32(h + 4 * NP_HDR_COMPRESSION, NP_COMPRESSION);
    wr32(h + 4 * NP_HDR_SALT,        NP_PROTOCOL_HIGH);
    wr32(h + 4 * NP_HDR_PROTOCOL,    NP_PROTOCOL_LOW);
    wr32(h + 4 * NP_HDR_IMPL,        NP_IMPL_TAG);
    np_send_raw(h, sizeof(h));
}

static void np_send_nick(void) {
    np_send_cmd(NP_CMD_NICK, np.nick, NP_NICK_LEN);
}

static void np_send_info(void) {
    uint8_t p[NP_INFO_SIZE] = {0};
    wr32(p, np.core.content_crc);
    snprintf((char *)p + 4, NP_NICK_LEN, "%s", np.core.core_name ? np.core.core_name : "");
    snprintf((char *)p + 4 + NP_NICK_LEN, NP_NICK_LEN, "%s",
             np.core.core_version ? np.core.core_version : "");
    np_send_cmd(NP_CMD_INFO, p, sizeof(p));
}

/** @brief Asks to play, on whatever device the host gives. */
static void np_send_play(void) {
    uint8_t p[4];
    wr32(p, 0);
    np_send_cmd(NP_CMD_PLAY, p, sizeof(p));
    np.play_asked = true;
}

bool netplay_client_start(const char *host, uint16_t port, const char *mitm_session,
                          const char *nick, const netplay_core_t *core) {
    struct addrinfo hints = {0}, *res = NULL;
    char service[8];
    int fd;

    netplay_client_stop();
    np_relay.on = false;
    if (mitm_session && mitm_session[0]) {
        uint8_t unique[NP_RELAY_UNIQUE_SIZE];
        if (!np_relay_decode(mitm_session, unique)) {
            fprintf(stderr, "[netplay] not a relay session: %s\n", mitm_session);
            return false;
        }
        np_relay_id(np_relay.id, NP_RELAY_SESSION, unique);
        np_relay.on = true;
    }

    np_session_reset();
    np.core = *core;
    snprintf(np.nick, sizeof(np.nick), "%s", nick && nick[0] ? nick : "gecnd");
    np.have_state = np.paused = np.play_asked = np.playing = np.reset_pending = false;
    np.stall = 0;
    np.crc_ok = np.crc_bad = 0;
    np.crc_known = np.crc_usable = false;
    free(np.pending);
    np.pending = NULL;
    free(np.unstable);
    np.unstable = NULL;
    memset(np.crcs, 0, sizeof(np.crcs));
    memset(np.history, 0, sizeof(np.history));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%u", port);
    if (getaddrinfo(host, service, &hints, &res) || !res) {
        fprintf(stderr, "[netplay] cannot resolve %s\n", host);
        return false;
    }
    fd = socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return false;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0 && errno != EINPROGRESS) {
        fprintf(stderr, "[netplay] cannot connect to %s:%u\n", host, port);
        close(fd);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    np.fd = fd;
    np.phase = NP_CONNECTING;
    np.last_recv = time(NULL);
    fprintf(stderr, "[netplay] connecting to %s:%u%s%s as %s\n", host, port,
            np_relay.on ? " relay session " : "", np_relay.on ? mitm_session : "", np.nick);
    return true;
}

void netplay_client_stop(void) {
    if (np.phase == NP_OFF) return;
    if (np.phase == NP_RUNNING) {
        np_send_cmd(NP_CMD_DISCONNECT, NULL, 0);
        np_flush();
    }
    np_close();
}

bool netplay_client_active(void) {
    return np.phase != NP_OFF;
}

/* ------------------------------------------------------------------ */
/* Players and input                                                   */
/* ------------------------------------------------------------------ */

/** @brief Whether every input of a frame is here. */
static bool np_frame_ready(uint32_t frame) {
    return frame < np.server_frame && np_inputs_ready(frame, np.client);
}

/** @brief Our input for a frame, from the local controllers (live) or
 * zero (a frame already run before we were told we play it). */
static void np_send_own(uint32_t frame, bool live) {
    uint8_t p[8 + 4 * NP_MAX_WORDS];
    unsigned size;

    wr32(p, frame);
    wr32(p + 4, np.client);
    size = np_local_input(np.client, &np.core, live, p + 8);
    np_store_input(np.client, frame, p + 8, size / 4);
    np_send_cmd(NP_CMD_INPUT, p, 8 + size);
}

int16_t netplay_client_input(unsigned port, unsigned device, unsigned index, unsigned id) {
    return np_session_input(port, device, index, id);
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static void np_on_crc(uint32_t frame, uint32_t hash);

static bool np_on_sync(const uint8_t *p, uint32_t size) {
    uint32_t frame, client;
    const uint8_t *q;

    if (size < NP_SYNC_MIN) return false;
    frame  = rd32(p);
    client = rd32(p + 4);
    np.paused = (client & NP_SYNC_PAUSED) != 0;
    np.client = client & ~NP_SYNC_PAUSED;
    if (np.client >= NP_MAX_CLIENTS) return false;

    q = p + 8;
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++, q += 4) {
        np_session.devices[d] = rd32(q);
        if (np.core.set_port_device) np.core.set_port_device(d, np_session.devices[d]);
    }
    q += NP_MAX_DEVICES;  /* share modes */
    for (unsigned d = 0; d < NP_MAX_DEVICES; d++, q += 4) {
        const uint32_t clients = rd32(q);
        for (unsigned c = 0; c < NP_MAX_CLIENTS; c++)
            if (clients & (1u << c)) {
                np_session.players[c].devices |= 1u << d;
                np_session.players[c].from = frame;
                np_session.players[c].until = UINT32_MAX;
            }
    }
    {
        char nick[NP_NICK_LEN + 1] = {0};
        memcpy(nick, q, NP_NICK_LEN);
        if (strcmp(nick, np.nick)) {
            fprintf(stderr, "[netplay] the host calls us %s\n", nick);
            memcpy(np.nick, nick, NP_NICK_LEN);
        }
    }
    q += NP_NICK_LEN;

    /* SRAM, when ours is the same size */
    {
        const size_t sram = size - (uint32_t)(q - p);
        if (sram && np.core.memory_size && np.core.memory_data &&
            np.core.memory_size(0) == sram && np.core.memory_data(0))
            memcpy(np.core.memory_data(0), q, sram);
    }

    np.self_frame = np.server_frame = np_session.run_frame = frame;
    fprintf(stderr, "[netplay] in sync at frame %u as client %u%s\n",
            frame, np.client, np.paused ? " (paused)" : "");
    return true;
}

static void np_on_mode(const uint8_t *p, uint32_t size) {
    uint32_t frame, flags, client, devices;
    char nick[NP_NICK_LEN + 1] = {0};

    if (size < NP_MODE_SIZE) return;
    frame   = rd32(p);
    flags   = rd32(p + 4);
    client  = flags & 0xffff;
    devices = rd32(p + 8);
    memcpy(nick, p + 12 + NP_MAX_DEVICES, NP_NICK_LEN);
    if (client >= NP_MAX_CLIENTS) return;

    if (flags & NP_MODE_PLAYING) {
        np_session.players[client].devices = devices;
        np_session.players[client].from = frame;
        np_session.players[client].until = UINT32_MAX;
    } else {
        np_session.players[client].until = frame;
    }

    if (flags & NP_MODE_YOU) {
        np.playing = (flags & NP_MODE_PLAYING) != 0;
        if (np.playing) np.next_send = frame;
        fprintf(stderr, "[netplay] %s from frame %u (devices 0x%x)\n",
                np.playing ? "playing" : "spectating", frame, devices);
    } else {
        fprintf(stderr, "[netplay] %s is %s from frame %u\n", nick,
                (flags & NP_MODE_PLAYING) ? "playing" : "spectating", frame);
    }
}

/**
 * @brief Whether the 8 byte word holding a byte looks like a user space
 * address on both sides: a pointer the core saved as is, which differs
 * between processes without meaning anything for the game.
 */
static bool np_both_pointers(const uint8_t *a, const uint8_t *b, size_t size, size_t at) {
    const size_t o = at & ~(size_t)7;
    uint64_t va = 0, vb = 0;

    if (sizeof(void *) != 8 || o + 8 > size) return false;
    memcpy(&va, a + o, 8);
    memcpy(&vb, b + o, 8);
    return va >> 47 == 0 && vb >> 47 == 0 && va > 0x10000 && vb > 0x10000;
}

/** @brief Bytes that differ between two states of the same size. */
static unsigned np_state_diff(const uint8_t *a, const uint8_t *b, size_t size) {
    unsigned n = 0;
    for (size_t i = 0; i < size; i++) n += a[i] != b[i];
    return n;
}

/**
 * @brief Loads a host savestate as the start of a frame. The first one
 * also tells whether the core's states are byte stable (some cores save
 * pointers, which no two processes share): without that the host's CRC
 * checks can never match, so they are left off.
 */
static void np_load_state(const uint8_t *state, size_t size, uint32_t frame) {
    if (!np.core.unserialize(state, size)) {
        fprintf(stderr, "[netplay] the core refused the host's savestate\n");
        return;
    }
    if (!np.crc_known && np.core.serialize && reserve(&np.state, &np.state_cap, size) &&
        np.core.serialize(np.state, size)) {
        np.stable_bytes = np_state_diff(np.state, state, size);
        np.crc_usable = np.stable_bytes == 0;
        np.crc_known = true;
        np.unstable = calloc(1, size);
        if (np.unstable)
            for (size_t i = 0; i < size; i++)
                np.unstable[i] = np.state[i] != state[i];
        if (!np.crc_usable)
            fprintf(stderr, "[netplay] the core's states change %u bytes on a round trip "
                    "(saved pointers?): CRC checks off\n", np.stable_bytes);
    }
    np.self_frame = frame;
    if (np.playing && np.next_send < frame) np.next_send = frame;
    np.have_state = true;
    fprintf(stderr, "[netplay] loaded the host's savestate for frame %u\n", frame);
}

/**
 * @brief A host savestate for a frame still ahead waits for it: once we
 * reach it, our own state is compared with the host's (a sync check that
 * works even for cores whose CRCs cannot), then the host's is loaded.
 */
static void np_load_pending(void) {
    if (!np.pending || np.self_frame < np.pending_frame) return;
    if (np.self_frame == np.pending_frame && np.core.serialize &&
        reserve(&np.state, &np.state_cap, np.pending_size) &&
        np.core.serialize(np.state, np.pending_size)) {
        unsigned diff = 0, real = 0;
        size_t first[4];
        for (size_t i = 0; i < np.pending_size; i++) {
            if (np.state[i] == np.pending[i]) continue;
            diff++;
            if (np.unstable && np.unstable[i]) continue;  /* a saved pointer */
            if (np_both_pointers(np.state, np.pending, np.pending_size, i)) continue;
            if (real < 4) first[real] = i;
            real++;
        }
        /* a few bytes may still differ where the host replayed frames
         * with the core's audio off (rollback): audio buffers, not the
         * game; a real desync grows */
        fprintf(stderr, "[netplay] sync check at frame %u: %u bytes differ, %u beyond saved "
                "pointers%s", np.pending_frame, diff, real, real ? " (at" : "\n");
        for (unsigned i = 0; i < real && i < 4; i++)
            fprintf(stderr, " 0x%zx%s", first[i], i + 1 < real && i < 3 ? "," : ")\n");
    }
    np_load_state(np.pending, np.pending_size, np.pending_frame);
    free(np.pending);
    np.pending = NULL;
}

static void np_on_savestate(const uint8_t *p, uint32_t size) {
    uint32_t frame, raw;
    size_t ours;
    const uint8_t *state = p + 8;
    uint8_t *inflated = NULL;

    if (size < 8) return;
    frame = rd32(p);
    raw   = rd32(p + 4);  /* the state's size, uncompressed */
    ours  = np.core.serialize_size ? np.core.serialize_size() : 0;
    if (raw != ours || !np.core.unserialize) {
        fprintf(stderr, "[netplay] savestate of %u bytes, ours is %zu: cannot load it\n", raw, ours);
        return;
    }
    if (np.compression) {
#ifdef GECND_NETPLAY_ZLIB
        uLongf len = raw;
        inflated = malloc(raw);
        if (!inflated || uncompress(inflated, &len, p + 8, size - 8) != Z_OK || len != raw) {
            fprintf(stderr, "[netplay] cannot inflate the host's savestate\n");
            free(inflated);
            return;
        }
        state = inflated;
#endif
    } else if (size - 8 != raw) {
        fprintf(stderr, "[netplay] truncated savestate\n");
        return;
    }
    /* joining, or a frame we already passed: load it now (the frame we
     * are about to run waits a moment, to be compared first) */
    if (!np.have_state || frame < np.self_frame) {
        np_load_state(state, raw, frame);
        free(inflated);
        return;
    }
    free(np.pending);
    np.pending = malloc(raw);
    if (!np.pending) {
        free(inflated);
        return;
    }
    memcpy(np.pending, state, raw);
    np.pending_frame = frame;
    np.pending_size = raw;
    free(inflated);
}

static void np_on_command(uint32_t cmd, const uint8_t *p, uint32_t size) {
    switch (cmd) {
    case NP_CMD_INPUT:
        if (size >= 8) {
            const uint32_t frame = rd32(p), client = rd32(p + 4) & 0xffff;
            if (client < NP_MAX_CLIENTS && client != np.client) {
                const unsigned words = np_words_for(np_session.players[client].devices);
                np_store_input(client, frame, p + 8,
                               words < (size - 8) / 4 ? words : (size - 8) / 4);
                if (client == 0 && frame + 1 > np.server_frame) np.server_frame = frame + 1;
            }
        }
        break;
    case NP_CMD_NOINPUT:
        if (size >= 4 && rd32(p) + 1 > np.server_frame) np.server_frame = rd32(p) + 1;
        break;
    case NP_CMD_MODE:
        np_on_mode(p, size);
        break;
    case NP_CMD_MODE_REFUSED:
        fprintf(stderr, "[netplay] the host refused to let us play (reason %u)\n",
                size >= 4 ? rd32(p) : 0);
        break;
    case NP_CMD_LOAD_SAVESTATE:
        np_on_savestate(p, size);
        break;
    case NP_CMD_PAUSE:
        np.paused = true;
        break;
    case NP_CMD_RESUME:
        np.paused = false;
        break;
    case NP_CMD_STALL:
        if (size >= 4) np.stall += rd32(p);
        break;
    case NP_CMD_RESET:
        if (size >= 4) {
            np.reset_pending = true;
            np.reset_frame = rd32(p);
        }
        break;
    case NP_CMD_CRC:
        if (size >= 8)
            np_on_crc(rd32(p), rd32(p + 4));
        break;
    case NP_CMD_PING_REQUEST:
        np_send_cmd(NP_CMD_PING_RESPONSE, NULL, 0);
        break;
    case NP_CMD_PLAYER_CHAT:
        if (size > NP_NICK_LEN)
            fprintf(stderr, "[netplay] %.*s: %.*s\n", NP_NICK_LEN, (const char *)p,
                    (int)(size - NP_NICK_LEN), (const char *)p + NP_NICK_LEN);
        break;
    case NP_CMD_NAK:
        np_fail("the host refused a command (NAK)");
        break;
    case NP_CMD_DISCONNECT:
        np_fail("the host closed the session");
        break;
    default:  /* CRC, settings, chat replies...: nothing to do */
        break;
    }
}

/**
 * @brief Consumes what arrived: the header, then whole commands.
 */
static void np_parse(void) {
    size_t used = 0;

    if (np.phase == NP_HEADER) {
        if (np.in_len >= 4 && rd32(np.in) == NP_MAGIC_FULL) {
            np_fail("the host is full");
            return;
        }
        if (np.in_len < NP_HDR_WORDS * 4) return;
        if (rd32(np.in + 4 * NP_HDR_MAGIC) != NP_MAGIC_RANP) {
            np_fail("not a netplay host");
            return;
        }
        np.protocol = rd32(np.in + 4 * NP_HDR_PROTOCOL);
        np.compression = rd32(np.in + 4 * NP_HDR_COMPRESSION) & NP_COMPRESSION;
        if (np.protocol < NP_PROTOCOL_LOW || np.protocol > NP_PROTOCOL_HIGH) {
            np_fail("the host speaks another netplay protocol");
            return;
        }
        if (rd32(np.in + 4 * NP_HDR_SALT)) {
            np_fail("the host wants a password, not supported yet");
            return;
        }
        used = NP_HDR_WORDS * 4;
        np.phase = NP_NICK;
        np_send_nick();
    }

    while (np.fd >= 0 && np.in_len - used >= 8) {
        const uint32_t cmd  = rd32(np.in + used);
        const uint32_t size = rd32(np.in + used + 4);
        const uint8_t *p    = np.in + used + 8;

        if (size > 64u * 1024 * 1024) {
            np_fail("command too big");
            return;
        }
        if (np.in_len - used - 8 < size) break;
        used += 8 + size;

        switch (np.phase) {
        case NP_NICK:
            if (cmd != NP_CMD_NICK) { np_fail("expected NICK"); return; }
            np.phase = NP_INFO;
            break;
        case NP_INFO:
            if (cmd != NP_CMD_INFO) { np_fail("expected INFO"); return; }
            if (size >= NP_INFO_SIZE) {
                char name[NP_NICK_LEN + 1] = {0}, version[NP_NICK_LEN + 1] = {0};
                memcpy(name, p + 4, NP_NICK_LEN);
                memcpy(version, p + 4 + NP_NICK_LEN, NP_NICK_LEN);
                fprintf(stderr, "[netplay] host runs %s %s, content crc %08x (ours %08x)\n",
                        name, version, rd32(p), np.core.content_crc);
                if (strcasecmp(name, np.core.core_name ? np.core.core_name : "")) {
                    np_fail("the host runs another core");
                    return;
                }
            }
            np_send_info();
            np.phase = NP_SYNC;
            break;
        case NP_SYNC:
            if (cmd != NP_CMD_SYNC || !np_on_sync(p, size)) { np_fail("bad SYNC"); return; }
            np.phase = NP_RUNNING;
            np_send_play();
            break;
        case NP_RUNNING:
            np_on_command(cmd, p, size);
            break;
        default:
            break;
        }
    }

    if (np.fd >= 0 && used) {
        memmove(np.in, np.in + used, np.in_len - used);
        np.in_len -= used;
    }
}

/** @brief Reads, finishes connecting, parses and flushes. */
static void np_io(void) {
    if (np.phase == NP_CONNECTING) {
        struct pollfd pfd = { .fd = np.fd, .events = POLLOUT };
        int err = 0;
        socklen_t len = sizeof(err);

        if (poll(&pfd, 1, 0) <= 0) return;
        getsockopt(np.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err) {
            np_fail(strerror(err));
            return;
        }
        np.phase = NP_HEADER;
        /* through a relay: say which session, then it is the host */
        if (np_relay.on) np_send_raw(np_relay.id, sizeof(np_relay.id));
        np_send_header();
    }

    for (;;) {
        ssize_t n;
        if (!reserve(&np.in, &np.in_cap, np.in_len + 65536)) {
            np_fail("out of memory");
            return;
        }
        n = recv(np.fd, np.in + np.in_len, np.in_cap - np.in_len, 0);
        if (n > 0) {
            np.in_len += (size_t)n;
            np.last_recv = time(NULL);
            continue;
        }
        if (n == 0) {
            np_fail("the host closed the connection");
            return;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            np_fail(strerror(errno));
            return;
        }
        break;
    }

    np_parse();
    if (np.fd >= 0 && time(NULL) - np.last_recv > NP_TIMEOUT)
        np_fail("the host went silent");
    np_flush();
}

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */

/** @brief Compares a host CRC with ours; out of sync, asks for its state. */
static void np_compare_crc(uint32_t frame, uint32_t host, uint32_t ours) {
    if (!np.crc_usable) return;
    if (host == ours) {
        np.crc_ok++;
        return;
    }
    np.crc_bad++;
    fprintf(stderr, "[netplay] out of sync at frame %u (host %08x, ours %08x), "
            "asking the host for its state\n", frame, host, ours);
    np_send_cmd(NP_CMD_REQUEST_SAVESTATE, NULL, 0);
}

/**
 * @brief A CRC of the host's state at the start of a frame. The host only
 * sends it once it has every input of that frame, ours included, so it
 * usually comes after we ran the frame: then our CRC is in the history;
 * if the frame is still ahead, it waits for it.
 */
static void np_on_crc(uint32_t frame, uint32_t hash) {
    const unsigned h = (frame / NP_HISTORY_STEP) % NP_HISTORY;

    if (frame < np.self_frame) {
        if (np.history[h].set && np.history[h].frame == frame)
            np_compare_crc(frame, hash, np.history[h].hash);
        return;
    }
    for (unsigned i = 0; i < NP_CRCS; i++)
        if (!np.crcs[i].set) {
            np.crcs[i].frame = frame;
            np.crcs[i].hash  = hash;
            np.crcs[i].set   = true;
            return;
        }
}

/**
 * @brief Before running a frame: our state's CRC, kept every
 * NP_HISTORY_STEP frames and checked against a host CRC already here.
 */
static void np_check_crc(uint32_t frame) {
    bool wanted = frame % NP_HISTORY_STEP == 0;
    size_t size;
    uint32_t ours;

    for (unsigned i = 0; i < NP_CRCS; i++)
        if (np.crcs[i].set && np.crcs[i].frame <= frame) {
            if (np.crcs[i].frame == frame) wanted = true;
            else np.crcs[i].set = false;  /* jumped past it (a savestate) */
        }
    if (!wanted || !np.crc_usable || !np.core.serialize || !np.core.serialize_size) return;

    size = np.core.serialize_size();
    if (!reserve(&np.state, &np.state_cap, size) || !np.core.serialize(np.state, size))
        return;
    ours = netplay_crc32(np.state, size);

    if (frame % NP_HISTORY_STEP == 0) {
        const unsigned h = (frame / NP_HISTORY_STEP) % NP_HISTORY;
        np.history[h].frame = frame;
        np.history[h].hash  = ours;
        np.history[h].set   = true;
    }
    for (unsigned i = 0; i < NP_CRCS; i++)
        if (np.crcs[i].set && np.crcs[i].frame == frame) {
            np.crcs[i].set = false;
            np_compare_crc(frame, np.crcs[i].hash, ours);
        }
}

static void np_run_frame(void) {
    uint32_t frame;

    np_load_pending();
    frame = np.self_frame;
    np_check_crc(frame);

    if (np.playing) {
        /* frames the host says we play but that already ran without us
         * ran with an idle pad: tell the host exactly that */
        while (np.next_send < frame) np_send_own(np.next_send++, false);
        if (np.next_send == frame) np_send_own(np.next_send++, true);
    }
    if (np.reset_pending && np.reset_frame == frame) {
        np.reset_pending = false;
        if (np.core.reset) np.core.reset();
    }
    np_session.run_frame = frame;
    np.core.run();
    np.self_frame++;
    if (np.self_frame % 600 == 0)
        fprintf(stderr, "[netplay] frame %u, host at %u, CRC checks %u ok / %u off%s\n",
                np.self_frame, np.server_frame, np.crc_ok, np.crc_bad,
                np.crc_usable ? "" : " (not usable with this core)");
}

void netplay_client_tick(void) {
    unsigned ran = 0;

    if (np.phase == NP_OFF) return;
    np_io();
    if (np.phase != NP_RUNNING || !np.have_state || np.paused) return;
    if (np.stall) {
        np.stall--;
        return;
    }

    /* our input, read now, for the frames up to NP_INPUT_LEAD ahead */
    if (np.playing) {
        while (np.next_send < np.self_frame) np_send_own(np.next_send++, false);
        while (np.next_send < np.self_frame + NP_INPUT_LEAD) np_send_own(np.next_send++, true);
    }

    /* one frame per tick; more only to catch up with the host */
    while (ran < NP_MAX_CATCHUP && np_frame_ready(np.self_frame)) {
        np_run_frame();
        ran++;
        if (np.server_frame - np.self_frame <= 2) break;
    }
    np_flush();
}

#endif
