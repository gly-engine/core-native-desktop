#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include <linux/joystick.h>

#include "gecnd.h"

#define JOY_MAX_INSTANCES  4
#define JOY_MAX_BUTTONS    64
#define JOY_MAX_AXES       64

/*
 * hex do [keymap.*] no toml:
 *
 *   0x01NN  botao NN            (byte alto = js_event.type)
 *   0x02NN  eixo NN negativo    (esquerda / cima)
 *   0x03NN  eixo NN positivo    (direita / baixo)
 */
#define JOY_CODE_BUTTON(n)   (0x0100u | (uint32_t)(n))
#define JOY_CODE_AXIS_NEG(n) (0x0200u | (uint32_t)(n))
#define JOY_CODE_AXIS_POS(n) (0x0300u | (uint32_t)(n))

typedef struct {
    int       port;
    int       running;
    int       retry_ms;
    int       deadzone;
    int       emit_init;
    int       fd;
    int       wake[2];
    int       connected;
    uint64_t  btn_held;
    int8_t    axis_dir[JOY_MAX_AXES];  /* -1, 0, +1 */
    char      device[256];
    pthread_t thread;
} joy_instance_t;

static joy_instance_t g_instances[JOY_MAX_INSTANCES];

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

/* "/dev/input/js0" | "/input/js0" | "input/js0" | "js0" -> "/dev/input/js0" */
static void joy_normalize_device(const char *in, char *out, size_t outsz)
{
    if (strncmp(in, "/dev/", 5) == 0)
        snprintf(out, outsz, "%s", in);
    else if (in[0] == '/')
        snprintf(out, outsz, "/dev%s", in);
    else if (strncmp(in, "js", 2) == 0)
        snprintf(out, outsz, "/dev/input/%s", in);
    else
        snprintf(out, outsz, "/dev/%s", in);
}

static void joy_axis_set(joy_instance_t *inst, uint8_t number, int8_t dir, bool emit)
{
    int8_t old = inst->axis_dir[number];
    if (old == dir) return;
    inst->axis_dir[number] = dir;
    if (!emit) return;

    if (old < 0) gamely_daemon_input_push(JOY_CODE_AXIS_NEG(number), false, 0);
    if (old > 0) gamely_daemon_input_push(JOY_CODE_AXIS_POS(number), false, 0);
    if (dir < 0) gamely_daemon_input_push(JOY_CODE_AXIS_NEG(number), true, 0);
    if (dir > 0) gamely_daemon_input_push(JOY_CODE_AXIS_POS(number), true, 0);
}

static void joy_feed(joy_instance_t *inst, const struct js_event *ev)
{
    /* JS_EVENT_INIT: rajada sintetica do kernel no open com o estado atual.
     * semeia o estado sem disparar tecla, a nao ser com ?init=1 */
    bool emit = inst->emit_init || !(ev->type & JS_EVENT_INIT);
    uint8_t type = ev->type & ~JS_EVENT_INIT;

    if (type == JS_EVENT_BUTTON) {
        if (ev->number >= JOY_MAX_BUTTONS) return;
        bool pressed = ev->value != 0;
        if (pressed) inst->btn_held |=  (uint64_t)1 << ev->number;
        else         inst->btn_held &= ~((uint64_t)1 << ev->number);
        if (emit)
            gamely_daemon_input_push(JOY_CODE_BUTTON(ev->number), pressed, 0);
    }
    else if (type == JS_EVENT_AXIS) {
        if (ev->number >= JOY_MAX_AXES) return;
        int8_t dir = 0;
        if      (ev->value <= -inst->deadzone) dir = -1;
        else if (ev->value >=  inst->deadzone) dir =  1;
        joy_axis_set(inst, ev->number, dir, emit);
    }
}

/* device sumiu, nao vem release nenhum */
static void joy_release_all(joy_instance_t *inst)
{
    for (int n = 0; n < JOY_MAX_BUTTONS; n++) {
        if (inst->btn_held & ((uint64_t)1 << n))
            gamely_daemon_input_push(JOY_CODE_BUTTON(n), false, 0);
    }
    inst->btn_held = 0;

    for (int n = 0; n < JOY_MAX_AXES; n++)
        joy_axis_set(inst, (uint8_t)n, 0, true);
}

/* dorme timeout_ms, mas acorda na hora se joy_close escrever no self-pipe */
static int joy_sleep(joy_instance_t *inst, int timeout_ms)
{
    struct pollfd pfd = { .fd = inst->wake[0], .events = POLLIN };
    if (poll(&pfd, 1, timeout_ms) > 0) return -1;
    return inst->running ? 0 : -1;
}

static int joy_try_connect(joy_instance_t *inst)
{
    int fd = open(inst->device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        /* ENOENT em loop e o caso normal de "ainda nao plugou" */
        if (errno != ENOENT)
            fprintf(stderr, "[core:input:joy] open failed: %s (%s)\n",
                    inst->device, strerror(errno));
        return -1;
    }
    inst->fd = fd;
    memset(inst->axis_dir, 0, sizeof(inst->axis_dir));
    inst->btn_held = 0;
    if (!inst->connected) {
        inst->connected = 1;
        fprintf(stderr, "[core:input:joy] %s connected (port=%d)\n", inst->device, inst->port);
    }
    return 0;
}

static void joy_disconnect(joy_instance_t *inst)
{
    joy_release_all(inst);
    close(inst->fd);
    inst->fd = -1;
    if (inst->connected) {
        inst->connected = 0;
        fprintf(stderr, "[core:input:joy] %s disconnected (port=%d)\n", inst->device, inst->port);
    }
}

static void *joy_thread(void *arg)
{
    joy_instance_t *inst = (joy_instance_t *)arg;

    while (inst->running) {
        if (inst->fd < 0) {
            if (joy_try_connect(inst) != 0) {
                if (joy_sleep(inst, inst->retry_ms) != 0) break;
                continue;
            }
        }

        struct pollfd pfd[2] = {
            { .fd = inst->wake[0], .events = POLLIN },
            { .fd = inst->fd,      .events = POLLIN }
        };
        int n = poll(pfd, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pfd[0].revents) break;
        if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            joy_disconnect(inst);
            if (joy_sleep(inst, inst->retry_ms) != 0) break;
            continue;
        }
        if (!(pfd[1].revents & POLLIN)) continue;

        struct js_event ev;
        ssize_t got = read(inst->fd, &ev, sizeof(ev));
        if (got == (ssize_t)sizeof(ev)) {
            joy_feed(inst, &ev);
        } else if (got < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            joy_disconnect(inst);
            if (joy_sleep(inst, inst->retry_ms) != 0) break;
        }
    }

    if (inst->fd >= 0) {
        joy_release_all(inst);
        close(inst->fd);
        inst->fd = -1;
    }
    return NULL;
}

static bool joy_open(int port, const char *searchparams)
{
    if (port < 0 || port >= JOY_MAX_INSTANCES) {
        fprintf(stderr, "[core:input:joy] invalid port %d\n", port);
        return false;
    }
    if (!searchparams) {
        fprintf(stderr, "[core:input:joy] dev= required\n");
        return false;
    }

    char device[256] = {0};
    if (!parse_param_str(searchparams, "dev", device, sizeof(device)) &&
        !parse_param_str(searchparams, "device", device, sizeof(device))) {
        fprintf(stderr, "[core:input:joy] dev= required\n");
        return false;
    }

    joy_instance_t *inst = &g_instances[port];
    if (inst->running) return false;

    memset(inst, 0, sizeof(*inst));
    joy_normalize_device(device, inst->device, sizeof(inst->device));
    inst->port      = port;
    inst->fd        = -1;
    inst->retry_ms  = parse_param_int(searchparams, "retry", 1000);
    inst->deadzone  = parse_param_int(searchparams, "deadzone", 8192);
    inst->emit_init = parse_param_int(searchparams, "init", 0) != 0;
    if (inst->retry_ms < 50)    inst->retry_ms = 50;
    if (inst->deadzone < 1)     inst->deadzone = 1;
    if (inst->deadzone > 32767) inst->deadzone = 32767;

    if (pipe(inst->wake) != 0) {
        fprintf(stderr, "[core:input:joy] pipe failed (%s)\n", strerror(errno));
        return false;
    }

    inst->running = 1;
    if (pthread_create(&inst->thread, NULL, joy_thread, inst) != 0) {
        fprintf(stderr, "[core:input:joy] pthread_create failed\n");
        inst->running = 0;
        close(inst->wake[0]);
        close(inst->wake[1]);
        return false;
    }

    /* sucesso mesmo com device ausente: a thread fica retentando */
    fprintf(stderr, "[core:input:joy] %s retry=%dms deadzone=%d (port=%d)\n",
            inst->device, inst->retry_ms, inst->deadzone, port);
    return true;
}

static void joy_close(int port)
{
    if (port < 0 || port >= JOY_MAX_INSTANCES) return;
    joy_instance_t *inst = &g_instances[port];
    if (!inst->running) return;

    inst->running = 0;
    if (write(inst->wake[1], "x", 1) < 0) { /* thread ja saiu */ }
    pthread_join(inst->thread, NULL);

    close(inst->wake[0]);
    close(inst->wake[1]);
    inst->wake[0] = inst->wake[1] = -1;
}

const gamely_input_driver_t gamely_driver_joy = { joy_open, joy_close };
