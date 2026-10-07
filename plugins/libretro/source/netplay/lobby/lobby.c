/**
 * @file plugins/libretro/source/netplay/lobby/lobby.c
 * @brief Announces the hosted room in the libretro lobby: an HTTP POST of
 * a form to /add, the fields RetroArch announces with.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gdweb.h"
#include "main.h"
#include "netplay/lobby/lobby.h"

#define NP_LOBBY_URL "http://lobby.libretro.com/add"

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
    lb_append(body, sizeof(body), "mitm_server", "");
    lb_append_uint(body, sizeof(body), "has_password", 0);
    lb_append_uint(body, sizeof(body), "has_spectate_password", 0);
    lb_append_uint(body, sizeof(body), "force_mitm", 0);
    /* not RetroArch: say which frontend this is */
    lb_append(body, sizeof(body), "retroarch_version", "gecnd");
    lb_append(body, sizeof(body), "frontend", "gecnd");
    lb_append(body, sizeof(body), "subsystem_name", "N/A");
    lb_append(body, sizeof(body), "mitm_session", "");
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
