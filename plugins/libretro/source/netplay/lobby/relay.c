/**
 * @file plugins/libretro/source/netplay/lobby/relay.c
 * @brief Relay ids, the base64 they are listed in, and the host's side of
 * a relay session.
 */
#include <string.h>

#include "netplay/common/session.h"
#include "netplay/lobby/lobby.h"
#include "netplay/lobby/relay.h"

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

/* ------------------------------------------------------------------ */
/* Host side of a relay session                                        */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

/* TODO: Winsock; until then netplay is off on Windows */
bool netplay_relay_host(const char *handle, netplay_relay_link_cb_t on_link,
                        netplay_relay_session_cb_t on_session, void *user) {
    (void)handle; (void)on_link; (void)on_session; (void)user;
    return false;
}
void netplay_relay_tick(void) {}
void netplay_relay_stop(void) {}
bool netplay_relay_wanted(void) { return false; }
const char *netplay_relay_session(void) { return NULL; }
const char *netplay_relay_handle(void) { return ""; }

#else

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>

/** @brief Clients waiting to be linked through the relay at once. */
#define NP_RELAY_PENDING 8

/**
 * @brief The relay session (netplay/lobby/relay.h): the control
 * connection, the session's id, and the links being opened for clients.
 */
static struct {
    bool     wanted;
    char     handle[32];
    int      fd;               /* control connection */
    bool     connecting;
    bool     ready;            /* the session's id is known */
    uint8_t  sid[NP_RELAY_ID_SIZE];
    char     session[17];      /* the id's unique bytes, base64 */
    struct sockaddr_storage addr;
    socklen_t addr_len;
    uint8_t  buf[NP_RELAY_ID_SIZE + NP_RELAY_ADDR_SIZE];
    size_t   got;
    netplay_relay_link_cb_t    on_link;
    netplay_relay_session_cb_t on_session;
    void    *user;
    struct {
        int      fd;
        uint8_t  id[NP_RELAY_ID_SIZE];
        bool     has_addr;
        time_t   since;
    } pending[NP_RELAY_PENDING];
} r = { .fd = -1, .pending = {
    { .fd = -1 }, { .fd = -1 }, { .fd = -1 }, { .fd = -1 },
    { .fd = -1 }, { .fd = -1 }, { .fd = -1 }, { .fd = -1 } } };



static void hr_close(const char *why) {
    if (why) fprintf(stderr, "[netplay] relay: %s\n", why);
    if (r.fd >= 0) close(r.fd);
    r.fd = -1;
    r.connecting = r.ready = false;
    r.got = 0;
    for (unsigned i = 0; i < NP_RELAY_PENDING; i++) {
        if (r.pending[i].fd >= 0) close(r.pending[i].fd);
        r.pending[i].fd = -1;
    }
}

/** @brief A non blocking connection to the relay. */
static int hr_connect(void) {
    int fd = socket(r.addr.ss_family, SOCK_STREAM, 0), one = 1;

    if (fd < 0) return -1;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (connect(fd, (struct sockaddr *)&r.addr, r.addr_len) < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    return fd;
}

/** @brief Where the relay is (from the lobby): open the control link. */
static void hr_on_tunnel(const char *addr, uint16_t port, void *user) {
    struct addrinfo hints = {0}, *res = NULL;
    char service[8];
    (void)user;

    if (!r.wanted) return;
    if (!addr) {
        hr_close("no address for it");
        return;
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%u", port);
    if (getaddrinfo(addr, service, &hints, &res) || !res) {
        hr_close("cannot resolve it");
        return;
    }
    memcpy(&r.addr, res->ai_addr, res->ai_addrlen);
    r.addr_len = res->ai_addrlen;
    freeaddrinfo(res);
    if ((r.fd = hr_connect()) < 0) {
        hr_close("cannot connect to it");
        return;
    }
    r.connecting = true;
    fprintf(stderr, "[netplay] relay: connecting to %s:%u\n", addr, port);
}

bool netplay_relay_host(const char *handle, netplay_relay_link_cb_t on_link,
                        netplay_relay_session_cb_t on_session, void *user) {
    if (!handle || !handle[0] || !on_link) return false;
    netplay_relay_stop();
    snprintf(r.handle, sizeof(r.handle), "%s", handle);
    r.on_link = on_link;
    r.on_session = on_session;
    r.user = user;
    r.wanted = true;
    return netplay_lobby_tunnel(handle, hr_on_tunnel, NULL);
}

void netplay_relay_stop(void) {
    hr_close(NULL);
    r.wanted = false;
}

bool netplay_relay_wanted(void) { return r.wanted; }
const char *netplay_relay_session(void) { return r.ready ? r.session : NULL; }
const char *netplay_relay_handle(void) { return r.handle; }

static bool hr_send(int fd, const void *data, size_t len) {
    return send(fd, data, len, MSG_NOSIGNAL) == (ssize_t)len;
}

/** @brief A client arrives (RATL): open its link and ask its address. */
static void hr_on_link(const uint8_t *id) {
    uint8_t ask[NP_RELAY_ID_SIZE];

    for (unsigned i = 0; i < NP_RELAY_PENDING; i++) {
        if (r.pending[i].fd >= 0) continue;
        if ((r.pending[i].fd = hr_connect()) < 0) {
            fprintf(stderr, "[netplay] relay: cannot open a link for a client\n");
            return;
        }
        memcpy(r.pending[i].id, id, NP_RELAY_ID_SIZE);
        r.pending[i].has_addr = false;
        r.pending[i].since = time(NULL);
        np_relay_id(ask, NP_RELAY_ADDR, id + 4);
        if (!hr_send(r.fd, ask, sizeof(ask))) hr_close("lost while asking a client's address");
        return;
    }
    fprintf(stderr, "[netplay] relay: too many clients arriving at once\n");
}

/** @brief The relay's messages on the control connection. */
static void hr_read(void) {
    for (;;) {
        size_t need;
        ssize_t n;

        if (!r.ready) need = NP_RELAY_ID_SIZE;
        else if (r.got < 4) need = 4;
        else switch (np_rd32(r.buf)) {
            case NP_RELAY_PING: need = 4; break;
            case NP_RELAY_LINK: need = NP_RELAY_ID_SIZE; break;
            case NP_RELAY_ADDR: need = NP_RELAY_ID_SIZE + NP_RELAY_ADDR_SIZE; break;
            default:
                hr_close("unknown message");
                return;
        }
        if (r.got < need) {
            n = recv(r.fd, r.buf + r.got, need - r.got, 0);
            if (n == 0) { hr_close("closed the session"); return; }
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) hr_close(strerror(errno));
                return;
            }
            r.got += (size_t)n;
            continue;
        }
        r.got = 0;

        if (!r.ready) {
            static const uint8_t zero[NP_RELAY_UNIQUE_SIZE];
            if (np_rd32(r.buf) != NP_RELAY_SESSION || !memcmp(r.buf + 4, zero, sizeof(zero))) {
                hr_close("refused a session");
                return;
            }
            memcpy(r.sid, r.buf, NP_RELAY_ID_SIZE);
            np_relay_encode(r.sid + 4, r.session);
            r.ready = true;
            fprintf(stderr, "[netplay] relay: session %s on %s\n", r.session, r.handle);
            if (r.on_session) r.on_session(r.handle, r.session, r.user);
            continue;
        }
        switch (np_rd32(r.buf)) {
        case NP_RELAY_PING:
            if (!hr_send(r.fd, r.buf, 4)) { hr_close("lost on a ping"); return; }
            break;
        case NP_RELAY_LINK:
            hr_on_link(r.buf);
            break;
        case NP_RELAY_ADDR:
            for (unsigned i = 0; i < NP_RELAY_PENDING; i++)
                if (r.pending[i].fd >= 0 && !r.pending[i].has_addr &&
                    !memcmp(r.pending[i].id + 4, r.buf + 4, NP_RELAY_UNIQUE_SIZE)) {
                    r.pending[i].has_addr = true;
                    break;
                }
            break;
        }
        if (r.fd < 0) return;
    }
}

/**
 * @brief Relay work in a tick: finish the control link, read the relay,
 * and hand links that are ready over as client connections.
 */
void netplay_relay_tick(void) {
    if (!r.wanted) return;
    if (r.fd >= 0 && r.connecting) {
        struct pollfd pfd = { .fd = r.fd, .events = POLLOUT };
        int err = 0;
        socklen_t len = sizeof(err);
        uint8_t ask[NP_RELAY_ID_SIZE];

        if (poll(&pfd, 1, 0) <= 0) return;
        getsockopt(r.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err) { hr_close(strerror(err)); return; }
        r.connecting = false;
        np_relay_id(ask, NP_RELAY_SESSION, NULL);  /* a new session, please */
        if (!hr_send(r.fd, ask, sizeof(ask))) { hr_close("lost asking for a session"); return; }
    }
    if (r.fd >= 0) hr_read();

    for (unsigned i = 0; i < NP_RELAY_PENDING; i++) {
        struct pollfd pfd = { .fd = r.pending[i].fd, .events = POLLOUT };
        if (r.pending[i].fd < 0) continue;
        if (time(NULL) - r.pending[i].since > 15) {
            close(r.pending[i].fd);
            r.pending[i].fd = -1;
            fprintf(stderr, "[netplay] relay: a client's link timed out\n");
            continue;
        }
        if (!r.pending[i].has_addr || poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLOUT)) continue;
        if (!hr_send(r.pending[i].fd, r.pending[i].id, NP_RELAY_ID_SIZE)) {
            close(r.pending[i].fd);
        } else {
            fprintf(stderr, "[netplay] relay: a client is linked\n");
            r.on_link(r.pending[i].fd, r.user);
        }
        r.pending[i].fd = -1;
    }
}

#endif
