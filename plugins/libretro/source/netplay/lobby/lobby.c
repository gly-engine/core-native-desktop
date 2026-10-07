/**
 * @file plugins/libretro/source/netplay/lobby/lobby.c
 * @brief Announces the hosted room in the libretro lobby: an HTTP POST of
 * a form to /add, the fields RetroArch announces with.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gdweb.h"
#include "main.h"
#include "netplay/lobby/lobby.h"

#define NP_LOBBY_URL    "http://lobby.libretro.com/add"
#define NP_LOBBY_LIST   "http://lobby.libretro.com/list/"
#define NP_LOBBY_TUNNEL "http://lobby.libretro.com/tunnel?name="

static struct {
    bool  active;
    bool  announced;     /* the lobby took it at least once */
    bool  in_flight;     /* a request is on its way */
    char  nick[32];
    char  core_name[64];
    char  core_version[64];
    char  game_name[128];
    uint32_t game_crc;
    uint16_t port;
    char  mitm_server[32];
    char  mitm_session[32];
    time_t next;         /* when to announce again */
    char  reply[512];
    size_t reply_len;
    int   status;
} lb;

static typeof(gdweb_control_client) *lb_client;

static bool lb_bind(void) {
    if (!lb_client) api->registry("get", "function:gdweb_control_client", (void *)&lb_client, NULL);
    return lb_client != NULL;
}

/** @brief Appends a form value, percent encoded; the lobby takes ASCII. */
static void lb_append(char *out, size_t size, const char *key, const char *value) {
    size_t n = strlen(out);
    n += (size_t)snprintf(out + n, n < size ? size - n : 0, "%s%s=", n ? "&" : "", key);
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p && n + 4 < size; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.' || *p == '~')
            out[n++] = (char)*p;
        else if (*p < 0x80)
            n += (size_t)snprintf(out + n, size - n, "%%%02X", *p);
        else
            out[n++] = '_';  /* not ASCII: the lobby would refuse the room */
    }
    out[n < size ? n : size - 1] = '\0';
}

static void lb_append_uint(char *out, size_t size, const char *key, unsigned value) {
    char num[16];
    snprintf(num, sizeof(num), "%u", value);
    lb_append(out, size, key, num);
}

static void lb_on_status(gdweb_id_t id, int status, void *user) {
    (void)id; (void)user;
    lb.status = status;
    lb.reply_len = 0;
}

static void lb_on_data(gdweb_id_t id, const char *data, size_t len, void *user) {
    (void)id; (void)user;
    if (lb.reply_len + len >= sizeof(lb.reply)) len = sizeof(lb.reply) - 1 - lb.reply_len;
    memcpy(lb.reply + lb.reply_len, data, len);
    lb.reply_len += len;
    lb.reply[lb.reply_len] = '\0';
}

static void lb_on_done(gdweb_id_t id, void *user) {
    (void)id; (void)user;
    lb.in_flight = false;
    if (lb.status != 200 || strncmp(lb.reply, "status=OK", 9)) {
        fprintf(stderr, "[netplay] the lobby did not take the room (HTTP %d)\n", lb.status);
        return;
    }
    if (!lb.announced) {
        const char *id_line = strstr(lb.reply, "id=");
        fprintf(stderr, "[netplay] room announced in the lobby (%.*s)\n",
                id_line ? (int)strcspn(id_line, "\n") : 2, id_line ? id_line : "ok");
    }
    lb.announced = true;
}

static void lb_on_error(gdweb_id_t id, const char *msg, void *user) {
    (void)id; (void)user;
    lb.in_flight = false;
    fprintf(stderr, "[netplay] cannot reach the lobby: %s\n", msg ? msg : "error");
}

static void lb_announce(unsigned players, unsigned spectators) {
    static char body[2048];
    char crc[16];
    gdweb_http_req_t req = {0};

    if (!lb_bind() || lb.in_flight) return;

    body[0] = '\0';
    snprintf(crc, sizeof(crc), "%08X", lb.game_crc);
    lb_append(body, sizeof(body), "username", lb.nick);
    lb_append(body, sizeof(body), "core_name", lb.core_name);
    lb_append(body, sizeof(body), "core_version", lb.core_version);
    lb_append(body, sizeof(body), "game_name", lb.game_name);
    lb_append(body, sizeof(body), "game_crc", crc);
    lb_append_uint(body, sizeof(body), "port", lb.port);
    lb_append(body, sizeof(body), "mitm_server", lb.mitm_server);
    lb_append_uint(body, sizeof(body), "has_password", 0);
    lb_append_uint(body, sizeof(body), "has_spectate_password", 0);
    /* the lobby only takes the relay's session with this set */
    lb_append_uint(body, sizeof(body), "force_mitm", lb.mitm_session[0] != '\0');
    /* not RetroArch: say which frontend this is */
    lb_append(body, sizeof(body), "retroarch_version", "gecnd");
    lb_append(body, sizeof(body), "frontend", "gecnd");
    lb_append(body, sizeof(body), "subsystem_name", "N/A");
    lb_append(body, sizeof(body), "mitm_session", lb.mitm_session);
    lb_append(body, sizeof(body), "mitm_custom_addr", "");
    lb_append_uint(body, sizeof(body), "mitm_custom_port", 0);
    lb_append_uint(body, sizeof(body), "player_count", players);
    lb_append_uint(body, sizeof(body), "spectator_count", spectators);

    req.method = "POST";
    req.body = body;
    req.body_len = strlen(body);
    req.content_type = "application/x-www-form-urlencoded";
    lb.in_flight = true;
    lb_client()->http(NP_LOBBY_URL, &req, lb_on_status, lb_on_data, lb_on_done, lb_on_error, NULL);
}

void netplay_lobby_start(const netplay_lobby_room_t *room) {
    memset(&lb, 0, sizeof(lb));
    snprintf(lb.nick, sizeof(lb.nick), "%s", room->nick ? room->nick : "gecnd");
    snprintf(lb.core_name, sizeof(lb.core_name), "%s", room->core_name ? room->core_name : "");
    snprintf(lb.core_version, sizeof(lb.core_version), "%s", room->core_version ? room->core_version : "");
    snprintf(lb.game_name, sizeof(lb.game_name), "%s", room->game_name ? room->game_name : "");
    lb.game_crc = room->game_crc;
    lb.port = room->port;
    snprintf(lb.mitm_server, sizeof(lb.mitm_server), "%s", room->mitm_server ? room->mitm_server : "");
    snprintf(lb.mitm_session, sizeof(lb.mitm_session), "%s", room->mitm_session ? room->mitm_session : "");
    lb.active = true;
    lb.next = 0;  /* right away */
}

void netplay_lobby_tick(unsigned players, unsigned spectators) {
    const time_t now = time(NULL);

    if (!lb.active || now < lb.next) return;
    lb.next = now + NP_LOBBY_PERIOD;
    lb_announce(players, spectators);
}

void netplay_lobby_stop(void) {
    lb.active = false;
}

/* ------------------------------------------------------------------ */
/* Relay lookup: "status=OK\ntunnel_addr=...\ntunnel_port=..."        */
/* ------------------------------------------------------------------ */

static struct {
    netplay_tunnel_cb_t cb;
    void  *user;
    int    status;
    char   reply[512];
    size_t len;
} tn;

static void tn_on_status(gdweb_id_t id, int status, void *user) {
    (void)id; (void)user;
    tn.status = status;
    tn.len = 0;
}

static void tn_on_data(gdweb_id_t id, const char *data, size_t len, void *user) {
    (void)id; (void)user;
    if (tn.len + len >= sizeof(tn.reply)) len = sizeof(tn.reply) - 1 - tn.len;
    memcpy(tn.reply + tn.len, data, len);
    tn.len += len;
    tn.reply[tn.len] = '\0';
}

static void tn_on_done(gdweb_id_t id, void *user) {
    const char *a = strstr(tn.reply, "tunnel_addr="), *p = strstr(tn.reply, "tunnel_port=");
    char addr[256] = "";
    (void)id; (void)user;

    if (tn.status != 200 || !a || !p) {
        fprintf(stderr, "[netplay] the lobby does not know that relay (HTTP %d)\n", tn.status);
        if (tn.cb) tn.cb(NULL, 0, tn.user);
        return;
    }
    a += strlen("tunnel_addr=");
    snprintf(addr, sizeof(addr), "%.*s", (int)strcspn(a, "\r\n"), a);
    if (tn.cb) tn.cb(addr, (uint16_t)atoi(p + strlen("tunnel_port=")), tn.user);
}

static void tn_on_error(gdweb_id_t id, const char *msg, void *user) {
    (void)id; (void)user;
    fprintf(stderr, "[netplay] cannot reach the lobby for the relay: %s\n", msg ? msg : "error");
    if (tn.cb) tn.cb(NULL, 0, tn.user);
}

bool netplay_lobby_tunnel(const char *handle, netplay_tunnel_cb_t cb, void *user) {
    char url[128];
    gdweb_http_req_t req = {0};

    if (!lb_bind() || !handle || !handle[0]) return false;
    snprintf(url, sizeof(url), "%s%s", NP_LOBBY_TUNNEL, handle);
    memset(&tn, 0, sizeof(tn));
    tn.cb = cb;
    tn.user = user;
    req.method = "GET";
    lb_client()->http(url, &req, tn_on_status, tn_on_data, tn_on_done, tn_on_error, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* Room list: the lobby's /list, a JSON array of rooms                 */
/* ------------------------------------------------------------------ */

static struct {
    int     state;
    int     status;
    char   *json;
    size_t  size, cap;
    unsigned fetch;      /* the fetch callbacks belong to */
} ls;

static void ls_on_status(gdweb_id_t id, int status, void *user) {
    (void)id;
    if ((uintptr_t)user != ls.fetch) return;
    ls.status = status;
    ls.size = 0;
}

static void ls_on_data(gdweb_id_t id, const char *data, size_t len, void *user) {
    (void)id;
    if ((uintptr_t)user != ls.fetch) return;
    if (ls.size + len + 1 > ls.cap) {
        size_t cap = ls.cap ? ls.cap : 65536;
        char *grown;
        while (cap < ls.size + len + 1) cap *= 2;
        if (!(grown = realloc(ls.json, cap))) {
            ls.state = NP_LOBBY_LIST_FAILED;
            return;
        }
        ls.json = grown;
        ls.cap = cap;
    }
    memcpy(ls.json + ls.size, data, len);
    ls.size += len;
    ls.json[ls.size] = '\0';
}

static void ls_on_done(gdweb_id_t id, void *user) {
    (void)id;
    if ((uintptr_t)user != ls.fetch || ls.state != NP_LOBBY_LIST_LOADING) return;
    ls.state = ls.status == 200 && ls.json ? NP_LOBBY_LIST_READY : NP_LOBBY_LIST_FAILED;
    if (ls.state == NP_LOBBY_LIST_FAILED)
        fprintf(stderr, "[netplay] the lobby did not give its room list (HTTP %d)\n", ls.status);
}

static void ls_on_error(gdweb_id_t id, const char *msg, void *user) {
    (void)id;
    if ((uintptr_t)user != ls.fetch) return;
    ls.state = NP_LOBBY_LIST_FAILED;
    fprintf(stderr, "[netplay] cannot reach the lobby for its rooms: %s\n", msg ? msg : "error");
}

void netplay_lobby_list_refresh(void) {
    gdweb_http_req_t req = {0};

    if (!lb_bind()) {
        ls.state = NP_LOBBY_LIST_FAILED;
        return;
    }
    /* a fetch still on its way is forgotten: its callbacks see another id */
    ls.fetch++;
    ls.state = NP_LOBBY_LIST_LOADING;
    ls.status = 0;
    ls.size = 0;
    req.method = "GET";
    lb_client()->http(NP_LOBBY_LIST, &req, ls_on_status, ls_on_data, ls_on_done, ls_on_error,
                      (void *)(uintptr_t)ls.fetch);
}

int netplay_lobby_list(const char **json, size_t *size) {
    *json = ls.state == NP_LOBBY_LIST_READY ? ls.json : NULL;
    *size = ls.state == NP_LOBBY_LIST_READY ? ls.size : 0;
    return ls.state;
}
