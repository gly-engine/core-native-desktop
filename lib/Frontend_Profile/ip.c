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

static char s_cached_ip[48] = {0};
static int  s_ip_ready      = 0;

const char *gecnd_profile_get_local_ip(void)
{
    if (!s_ip_ready) {
        platform_read_ip(s_cached_ip, sizeof(s_cached_ip));
        s_ip_ready = 1;
    }
    return s_cached_ip;
}

void gecnd_profile_ip_refresh(void)
{
    platform_read_ip(s_cached_ip, sizeof(s_cached_ip));
    s_ip_ready = 1;
}
