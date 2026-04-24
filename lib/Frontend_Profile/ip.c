#include <stdio.h>
#include <string.h>
#include "gemetrics.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")
#  endif
typedef int socklen_t;
#  define SOCK_CLOSE(s) closesocket(s)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  define INVALID_SOCKET (-1)
#  define SOCK_CLOSE(s)  close(s)
#endif

static void platform_read_ip(char *out, size_t size)
{
    out[0] = '\0';

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
#endif

    if (sock == INVALID_SOCKET) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return;
    }

    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family      = AF_INET;
    remote.sin_port        = htons(53);
    remote.sin_addr.s_addr = htonl(0x08080808u);

    if (connect(sock, (struct sockaddr *)&remote, sizeof(remote)) != 0) {
        SOCK_CLOSE(sock);
#if defined(_WIN32)
        WSACleanup();
#endif
        return;
    }

    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    memset(&local, 0, sizeof(local));

    if (getsockname(sock, (struct sockaddr *)&local, &len) == 0) {
        inet_ntop(AF_INET, &local.sin_addr, out, (socklen_t)size);
    }

    SOCK_CLOSE(sock);
#if defined(_WIN32)
    WSACleanup();
#endif
}

#define MAX_IPS 8

static char     s_cached_ip[48]        = {0};
static char     s_ip_list[MAX_IPS][48] = {{0}};
static int      s_ip_count             = 0;
static uint64_t s_last_refresh         = 0;

#define IP_REFRESH_MS 60000ULL

static void maybe_refresh(void)
{
    uint64_t now = gecnd_get_cur_time();
    if (now - s_last_refresh >= IP_REFRESH_MS) {
        s_last_refresh = now;
        gecnd_profile_ip_refresh();
    }
}

const char *gecnd_profile_get_local_ip(void)
{
    maybe_refresh();
    return s_cached_ip;
}

int gecnd_profile_get_ip_count(void)
{
    maybe_refresh();
    return s_ip_count;
}

const char *gecnd_profile_get_ip_at(int i)
{
    maybe_refresh();
    if (i < 0 || i >= s_ip_count) return "0.0.0.0";
    return s_ip_list[i];
}

void gecnd_profile_ip_refresh(void)
{
    s_ip_count    = 0;
    s_cached_ip[0] = '\0'; /* limpa antes de tentar, evita valor stale */

#if !defined(_WIN32)
    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) == 0) {
        for (struct ifaddrs *ifa = ifap; ifa && s_ip_count < MAX_IPS; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK)                         continue;

            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t addr = ntohl(sin->sin_addr.s_addr);
            if (addr == 0)              continue;
            if ((addr >> 16) == 0xA9FE) continue; /* link-local 169.254.x.x */

            inet_ntop(AF_INET, &sin->sin_addr,
                      s_ip_list[s_ip_count], sizeof(s_ip_list[0]));
            s_ip_count++;
        }
        freeifaddrs(ifap);
    }

    if (s_ip_count > 0)
        platform_read_ip(s_cached_ip, sizeof(s_cached_ip));

#else /* Windows */
    /* platform_read_ip usa UDP connect trick para achar o IP de saída */
    platform_read_ip(s_cached_ip, sizeof(s_cached_ip));

    if (s_cached_ip[0] && strcmp(s_cached_ip, "0.0.0.0") != 0) {
        strncpy(s_ip_list[0], s_cached_ip, sizeof(s_ip_list[0]) - 1);
        s_ip_list[0][sizeof(s_ip_list[0]) - 1] = '\0';
        s_ip_count = 1;
    }
#endif

    if (!s_cached_ip[0])
        strcpy(s_cached_ip, "0.0.0.0");
}

void gecnd_profile_ip_tick(void)
{
    maybe_refresh();
}