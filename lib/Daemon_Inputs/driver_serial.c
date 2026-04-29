#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#  include <pthread.h>
#  include <errno.h>
#  include <time.h>
#endif

#include "gecnd.h"

#define SERIAL_MAX_INSTANCES  4

typedef struct {
    int      port;
    int      running;
    int      bytes;
    int      ttl_ms;
    int      gap_ms;
    int      have;
    uint32_t code;
    uint32_t mask;
    uint64_t last_ms;
#if defined(_WIN32)
    HANDLE    handle;
    HANDLE    thread;
#else
    int       fd;
    pthread_t thread;
#endif
} serial_instance_t;

static uint64_t serial_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}

static void serial_feed(serial_instance_t *inst, uint8_t b)
{
    uint64_t now = serial_now_ms();
    if (inst->have > 0 && (now - inst->last_ms) > (uint64_t)inst->gap_ms) {
        inst->have = 0;
        inst->code = 0;
    }
    inst->last_ms = now;

    inst->code = ((inst->code << 8) | b) & inst->mask;
    inst->have++;

    if (inst->have >= inst->bytes) {
        gamely_daemon_input_push(inst->code, true, (uint32_t)inst->ttl_ms);
        inst->have = 0;
        inst->code = 0;
    }
}

static serial_instance_t g_instances[SERIAL_MAX_INSTANCES];

static int parse_param_str(const char *params, const char *key, char *out, size_t outsz)
{
    size_t klen = strlen(key);
    const char *p = params;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        if (seg > klen + 1 && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t vlen = seg - klen - 1;
            if (vlen >= outsz) vlen = outsz - 1;
            memcpy(out, p + klen + 1, vlen);
            out[vlen] = '\0';
            return 1;
        }
        p = amp ? amp + 1 : NULL;
    }
    return 0;
}

static int parse_param_int(const char *params, const char *key, int defval)
{
    char buf[32];
    if (!parse_param_str(params, key, buf, sizeof(buf))) return defval;
    return (int)strtol(buf, NULL, 10);
}

static void serial_window_init(serial_instance_t *inst, int bytes, int ttl_ms, int gap_ms)
{
    if (bytes < 1) bytes = 1;
    if (bytes > 4) bytes = 4;
    if (ttl_ms < 0) ttl_ms = 0;
    if (gap_ms < 1) gap_ms = 1;
    inst->bytes   = bytes;
    inst->ttl_ms  = ttl_ms;
    inst->gap_ms  = gap_ms;
    inst->have    = 0;
    inst->code    = 0;
    inst->last_ms = 0;
    inst->mask    = (bytes >= 4) ? 0xFFFFFFFFu : ((1u << (bytes * 8)) - 1u);
}

#if defined(_WIN32)

static int baud_to_dcb(int baud)
{
    switch (baud) {
        case 110:    return CBR_110;
        case 300:    return CBR_300;
        case 600:    return CBR_600;
        case 1200:   return CBR_1200;
        case 2400:   return CBR_2400;
        case 4800:   return CBR_4800;
        case 9600:   return CBR_9600;
        case 14400:  return CBR_14400;
        case 19200:  return CBR_19200;
        case 38400:  return CBR_38400;
        case 57600:  return CBR_57600;
        case 115200: return CBR_115200;
        default:     return CBR_9600;
    }
}

static DWORD WINAPI serial_thread(LPVOID arg)
{
    serial_instance_t *inst = (serial_instance_t *)arg;
    uint8_t b;
    DWORD got;
    while (inst->running) {
        if (!ReadFile(inst->handle, &b, 1, &got, NULL)) break;
        if (got == 1) serial_feed(inst, b);
    }
    return 0;
}

static bool serial_open(int port, const char *searchparams)
{
    if (port < 0 || port >= SERIAL_MAX_INSTANCES) {
        fprintf(stderr, "[core:input:serial] invalid port %d\n", port);
        return false;
    }
    if (!searchparams) {
        fprintf(stderr, "[core:input:serial] device= required\n");
        return false;
    }

    char device[64] = {0};
    if (!parse_param_str(searchparams, "device", device, sizeof(device))) {
        fprintf(stderr, "[core:input:serial] device= required\n");
        return false;
    }
    int baud  = parse_param_int(searchparams, "baudrate", 9600);
    int bytes = parse_param_int(searchparams, "bytes", 1);
    int ttl   = parse_param_int(searchparams, "ttl", 200);
    int gap   = parse_param_int(searchparams, "gap", 50);

    serial_instance_t *inst = &g_instances[port];
    if (inst->running) return false;
    serial_window_init(inst, bytes, ttl, gap);

    char path[80];
    if (strncmp(device, "\\\\.\\", 4) == 0)
        snprintf(path, sizeof(path), "%s", device);
    else
        snprintf(path, sizeof(path), "\\\\.\\%s", device);

    HANDLE h = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[core:input:serial] CreateFile failed: %s\n", path);
        return false;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return false; }
    dcb.BaudRate = baud_to_dcb(baud);
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(h, &dcb)) { CloseHandle(h); return false; }

    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout         = 50;
    to.ReadTotalTimeoutConstant    = 50;
    to.ReadTotalTimeoutMultiplier  = 0;
    SetCommTimeouts(h, &to);

    inst->handle  = h;
    inst->port    = port;
    inst->running = 1;
    inst->thread  = CreateThread(NULL, 0, serial_thread, inst, 0, NULL);
    if (!inst->thread) {
        inst->running = 0;
        CloseHandle(h);
        inst->handle = NULL;
        return false;
    }
    fprintf(stderr, "[core:input:serial] %s @ %d baud %d-byte ttl=%dms gap=%dms (port=%d)\n", path, baud, inst->bytes, inst->ttl_ms, inst->gap_ms, port);
    return true;
}

static void serial_close(int port)
{
    if (port < 0 || port >= SERIAL_MAX_INSTANCES) return;
    serial_instance_t *inst = &g_instances[port];
    if (!inst->running) return;
    inst->running = 0;
    if (inst->handle) {
        CancelIoEx(inst->handle, NULL);
        CloseHandle(inst->handle);
        inst->handle = NULL;
    }
    if (inst->thread) {
        WaitForSingleObject(inst->thread, 1000);
        CloseHandle(inst->thread);
        inst->thread = NULL;
    }
}

#else /* POSIX */

static int baud_to_speed(int baud)
{
    switch (baud) {
        case 50:     return B50;
        case 75:     return B75;
        case 110:    return B110;
        case 134:    return B134;
        case 150:    return B150;
        case 200:    return B200;
        case 300:    return B300;
        case 600:    return B600;
        case 1200:   return B1200;
        case 1800:   return B1800;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B9600;
    }
}

static void *serial_thread(void *arg)
{
    serial_instance_t *inst = (serial_instance_t *)arg;
    uint8_t b;
    while (inst->running) {
        ssize_t n = read(inst->fd, &b, 1);
        if (n == 1) {
            serial_feed(inst, b);
        } else if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
    }
    return NULL;
}

static bool serial_open(int port, const char *searchparams)
{
    if (port < 0 || port >= SERIAL_MAX_INSTANCES) {
        fprintf(stderr, "[core:input:serial] invalid port %d\n", port);
        return false;
    }
    if (!searchparams) {
        fprintf(stderr, "[core:input:serial] device= required\n");
        return false;
    }

    char device[256] = {0};
    if (!parse_param_str(searchparams, "device", device, sizeof(device))) {
        fprintf(stderr, "[core:input:serial] device= required\n");
        return false;
    }
    int baud  = parse_param_int(searchparams, "baudrate", 9600);
    int bytes = parse_param_int(searchparams, "bytes", 1);
    int ttl   = parse_param_int(searchparams, "ttl", 200);
    int gap   = parse_param_int(searchparams, "gap", 50);

    serial_instance_t *inst = &g_instances[port];
    if (inst->running) return false;
    serial_window_init(inst, bytes, ttl, gap);

    int fd = open(device, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "[core:input:serial] open failed: %s (%s)\n", device, strerror(errno));
        return false;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) { close(fd); return false; }

    cfmakeraw(&tio);
    speed_t spd = (speed_t)baud_to_speed(baud);
    cfsetispeed(&tio, spd);
    cfsetospeed(&tio, spd);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) { close(fd); return false; }
    tcflush(fd, TCIFLUSH);

    inst->fd      = fd;
    inst->port    = port;
    inst->running = 1;
    if (pthread_create(&inst->thread, NULL, serial_thread, inst) != 0) {
        inst->running = 0;
        close(fd);
        inst->fd = -1;
        return false;
    }
    fprintf(stderr, "[core:input:serial] %s @ %d baud %d-byte ttl=%dms gap=%dms (port=%d)\n", device, baud, inst->bytes, inst->ttl_ms, inst->gap_ms, port);
    return true;
}

static void serial_close(int port)
{
    if (port < 0 || port >= SERIAL_MAX_INSTANCES) return;
    serial_instance_t *inst = &g_instances[port];
    if (!inst->running) return;
    inst->running = 0;
    if (inst->fd > 0) {
        close(inst->fd);
        inst->fd = -1;
    }
    pthread_join(inst->thread, NULL);
}

#endif

const gamely_input_driver_t gamely_driver_serial = { serial_open, serial_close };
