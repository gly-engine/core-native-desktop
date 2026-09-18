#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/lirc.h>

#include "gecnd.h"
#include "driver_linux_lirc.h"

/*
 * hex in [keymap.*] in the toml: the remote's scancode, truncated by ?mask=.
 *
 *   lirc://vivensis.dtv30?dev=/dev/lirc0&ttl=200&mask=0xffff
 *
 * ?mode= picks how the code arrives (default auto, in this order):
 *
 *   scancode  kernel decodes (rc-core). only exists if the device exposes
 *             LIRC_CAN_REC_SCANCODE AND has a decoder enabled in sysfs.
 *   lirccode  hardware decodes on its own (old STB drivers).
 *   mode2     raw pulses: ?protocol= does the decoding (default nec, in
 *             driver_linux_lirc_nec.c). alias ?mode=2, the same "2" as the
 *             mode2(1) utility.
 *
 * the scancode emitted in mode2 is the SAME the kernel would emit in
 * scancode mode (ir_nec_bytes_to_scancode), so a keymap made on one box
 * holds on another.
 *
 * holding a key is kept alive by the ttl (?ttl=, default 200ms), renewed
 * on every frame; a NEC repeat arrives every ~108ms, so 200ms survives
 * losing one.
 *
 * ?strict=1 drops a NEC frame whose checksum (cmd vs ~cmd) does not check
 * out instead of taking it as NEC32. useful to tell whether the remote
 * really speaks NEC.
 *
 * ?debug=1 shows the decoded scancode; ?dump=1 also shows every raw
 * mode2 pulse/space — a NEC frame is ~67 lines, so only turn it on to
 * inspect the timing of a protocol the decoder did not recognize.
 *
 * IR has no release event: every frame becomes push(code, true, ttl) and
 * service_io releases the key once the ttl expires. a repeat frame only
 * extends the ttl (ttl_upsert), so holding the button holds the key.
 */

typedef struct {
    int       port;
    int       running;
    int       retry_ms;
    int       ttl_ms;
    int       repeat;
    int       debug;
    int       dump;        /* dump the raw mode2 pulse train */
    unsigned  decoder_flags;
    int       seen_repeat;  /* remote uses repeat frames to hold */
    uint32_t  mask;
    int       fd;
    uint32_t  mode;        /* mode in use */
    uint32_t  want_mode;   /* 0 = auto */
    const gamely_lirc_decoder_t *decoder;
    size_t    code_bytes;  /* read size in LIRCCODE */
    int       wake[2];
    int       connected;
    int       warned;      /* already complained about this connect attempt */
    char      device[256];
    pthread_t thread;
} lirc_instance_t;

static lirc_instance_t g_instances[LIRC_MAX_INSTANCES];

static const struct { const char *name; const gamely_lirc_decoder_t *dec; } k_decoders[] = {
    {"nec", &gamely_lirc_decoder_nec}
};

static const int k_decoder_count = (int)(sizeof(k_decoders) / sizeof(k_decoders[0]));

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

/* base 0: accepts 0xFF, 0377 and 255 */
static uint32_t parse_param_hex(const char *params, const char *key, uint32_t defval)
{
    char buf[32];
    if (!parse_param_str(params, key, buf, sizeof(buf))) return defval;
    return (uint32_t)strtoul(buf, NULL, 0);
}

/* name or number; 0 = auto */
static uint32_t parse_param_mode(const char *params)
{
    char buf[32];
    if (!parse_param_str(params, "mode", buf, sizeof(buf))) return 0;

    if (!strcmp(buf, "auto"))     return 0;
    if (!strcmp(buf, "scancode")) return LIRC_MODE_SCANCODE;
    if (!strcmp(buf, "lirccode")) return LIRC_MODE_LIRCCODE;
    if (!strcmp(buf, "mode2") ||
        !strcmp(buf, "raw")  ||
        !strcmp(buf, "2"))        return LIRC_MODE_MODE2;

    switch ((uint32_t)strtoul(buf, NULL, 0)) {
        case LIRC_MODE_MODE2:    return LIRC_MODE_MODE2;
        case LIRC_MODE_SCANCODE: return LIRC_MODE_SCANCODE;
        case LIRC_MODE_LIRCCODE: return LIRC_MODE_LIRCCODE;
        default: break;
    }
    fprintf(stderr, "[core:input:lirc] unknown mode=%s (auto|scancode|lirccode|mode2)\n", buf);
    return 0;
}

/* NULL = unknown name; a wrong config must not turn into silence */
static const gamely_lirc_decoder_t *parse_param_protocol(const char *params)
{
    char buf[32];
    if (!parse_param_str(params, "protocol", buf, sizeof(buf)))
        return k_decoders[0].dec;

    for (int i = 0; i < k_decoder_count; i++)
        if (!strcmp(k_decoders[i].name, buf)) return k_decoders[i].dec;

    fprintf(stderr, "[core:input:lirc] unknown protocol=%s (available:", buf);
    for (int i = 0; i < k_decoder_count; i++)
        fprintf(stderr, " %s", k_decoders[i].name);
    fprintf(stderr, ")\n");
    return NULL;
}

static const char *lirc_mode_name(uint32_t mode)
{
    switch (mode) {
        case LIRC_MODE_SCANCODE: return "scancode";
        case LIRC_MODE_LIRCCODE: return "lirccode";
        case LIRC_MODE_MODE2:    return "mode2";
        default:                 return "auto";
    }
}

/* "/dev/lirc0" | "/lirc0" | "lirc0" | "0" -> "/dev/lirc0" */
static void lirc_normalize_device(const char *in, char *out, size_t outsz)
{
    if (strncmp(in, "/dev/", 5) == 0)
        snprintf(out, outsz, "%s", in);
    else if (in[0] == '/')
        snprintf(out, outsz, "/dev%s", in);
    else if (in[0] >= '0' && in[0] <= '9')
        snprintf(out, outsz, "/dev/lirc%s", in);
    else
        snprintf(out, outsz, "/dev/%s", in);
}

static void lirc_emit(lirc_instance_t *inst, uint32_t code, bool repeat)
{
    if (inst->debug)
        fprintf(stderr, "[core:debug:input] lirc %s scancode= 0x%08X repeat= %d\n",
                lirc_mode_name(inst->mode), code, repeat);
    gamely_daemon_input_push(code & inst->mask, true, (uint32_t)inst->ttl_ms);
}

static void lirc_feed_scancode(lirc_instance_t *inst, const struct lirc_scancode *sc)
{
    bool repeat = (sc->flags & LIRC_SCANCODE_FLAG_REPEAT) != 0;
    if (repeat && !inst->repeat) return;
    lirc_emit(inst, (uint32_t)sc->scancode, repeat);
}

/* -- mode2: hand the pulse train to the protocol decoder -- */

/*
 * NEC already tells the two apart: a data frame is a new tap, a repeat
 * frame is "still holding". treating both alike forces a choice between a
 * short ttl (taps work, holds flicker) and a long ttl (holds are steady,
 * taps merge).
 *
 * so: if the remote uses repeat frames — which we learn by seeing one —
 * a data frame while the key is already down becomes release+press, and
 * every tap stays a tap no matter how generous the ttl is. a remote that
 * only resends data frames is left out of this and rides on the ttl.
 */
static void lirc_decoder_emit(void *usr, uint32_t scancode, bool repeat)
{
    lirc_instance_t *inst = (lirc_instance_t *)usr;

    if (repeat) {
        inst->seen_repeat = 1;
        if (!inst->repeat) return;
    } else if (inst->seen_repeat) {
        gamely_daemon_input_push(scancode & inst->mask, false, 0);
    }
    lirc_emit(inst, scancode, repeat);
}

static void lirc_mode2_feed(lirc_instance_t *inst, uint32_t entry)
{
    uint32_t tag = LIRC_MODE2(entry);

    if (inst->dump)
        fprintf(stderr, "[core:debug:input] lirc mode2 %s %u\n",
                tag == LIRC_MODE2_PULSE   ? "pulse"   :
                tag == LIRC_MODE2_SPACE   ? "space"   :
                tag == LIRC_MODE2_TIMEOUT ? "timeout" : "other",
                LIRC_VALUE(entry));

    inst->decoder->feed(inst->port, entry, inst->decoder_flags, lirc_decoder_emit, inst);
}

/* sleep timeout_ms, but wake at once if lirc_close writes to the self-pipe */
static int lirc_sleep(lirc_instance_t *inst, int timeout_ms)
{
    struct pollfd pfd = { .fd = inst->wake[0], .events = POLLIN };
    if (poll(&pfd, 1, timeout_ms) > 0) return -1;
    return inst->running ? 0 : -1;
}

static int lirc_lirccode_len(lirc_instance_t *inst)
{
    uint32_t bits = 32;
    if (ioctl(inst->fd, LIRC_GET_LENGTH, &bits) != 0 || bits == 0 || bits > 64) {
        inst->warned = 1;
        fprintf(stderr, "[core:input:lirc] LIRC_GET_LENGTH failed: %s\n", inst->device);
        return -1;
    }
    inst->code_bytes = (size_t)((bits + 7) / 8);
    return 0;
}

/*
 * auto picks the most chewed-up mode the device offers. with ?mode= the
 * choice is the user's and features is only a warning: an old driver
 * sometimes lies in LIRC_GET_FEATURES but honours LIRC_SET_REC_MODE.
 */
static int lirc_set_mode(lirc_instance_t *inst)
{
    uint32_t features = 0;

    if (inst->warned) return -1;  /* already failed and logged; just retry quietly */

    if (ioctl(inst->fd, LIRC_GET_FEATURES, &features) != 0) {
        inst->warned = 1;
        fprintf(stderr, "[core:input:lirc] LIRC_GET_FEATURES failed: %s (%s)\n",
                inst->device, strerror(errno));
        return -1;
    }

    if (inst->want_mode) {
        inst->mode = inst->want_mode;
        if (!(features & LIRC_MODE2REC(inst->mode)))
            fprintf(stderr, "[core:input:lirc] %s does not advertise %s "
                            "(features=0x%08X); trying anyway\n",
                    inst->device, lirc_mode_name(inst->mode), features);
    } else if (features & LIRC_CAN_REC_SCANCODE) {
        inst->mode = LIRC_MODE_SCANCODE;
    } else if (features & LIRC_CAN_REC_LIRCCODE) {
        inst->mode = LIRC_MODE_LIRCCODE;
    } else if (features & LIRC_CAN_REC_MODE2) {
        inst->mode = LIRC_MODE_MODE2;
    } else {
        inst->warned = 1;
        fprintf(stderr, "[core:input:lirc] %s has no usable rec mode (features=0x%08X)\n",
                inst->device, features);
        return -1;
    }

    if (inst->mode == LIRC_MODE_LIRCCODE && lirc_lirccode_len(inst) != 0)
        return -1;

    if (ioctl(inst->fd, LIRC_SET_REC_MODE, &inst->mode) != 0) {
        inst->warned = 1;
        fprintf(stderr, "[core:input:lirc] LIRC_SET_REC_MODE %s failed: %s (%s)\n",
                lirc_mode_name(inst->mode), inst->device, strerror(errno));
        return -1;
    }
    inst->decoder->reset(inst->port);
    return 0;
}

/*
 * in scancode mode the kernel does the decoding, and only if the rc device
 * has a decoder enabled. with [lirc] alone the chardev hands out raw
 * pulses and no scancode ever arrives — silence identical to "nobody
 * pressed anything". read the rc device's sysfs to turn that silence into
 * a warning. best-effort.
 */
static void lirc_warn_protocols(lirc_instance_t *inst)
{
    const char *base = strrchr(inst->device, '/');
    char  path[320];
    char  buf[512];
    FILE *f;
    size_t n;

    snprintf(path, sizeof(path), "/sys/class/lirc/%s/device/protocols",
             base ? base + 1 : inst->device);
    f = fopen(path, "r");
    if (!f) return;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    /* enabled ones come in brackets: "rc-5 nec [lirc] sony" */
    if (strstr(buf, "[") && !strstr(buf, "[lirc]"))
        return;
    if (!strstr(buf, "["))
        fprintf(stderr, "[core:input:lirc] %s has no kernel decoder enabled\n", inst->device);
    else
        fprintf(stderr, "[core:input:lirc] %s only has [lirc] (raw) enabled\n", inst->device);
    fprintf(stderr, "[core:input:lirc] no scancode will arrive; enable a protocol "
                    "(ir-keytable -p nec) or use ?mode=mode2\n");
}

static int lirc_try_connect(lirc_instance_t *inst)
{
    int fd = open(inst->device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        /* retrying quietly is for hotplug, but the 1st failure always
         * shows: otherwise a wrong dev= looks exactly like "receiver not
         * plugged in yet" */
        if (!inst->warned) {
            inst->warned = 1;
            fprintf(stderr, "[core:input:lirc] open failed: %s (%s) — retrying every %dms\n",
                    inst->device, strerror(errno), inst->retry_ms);
        }
        return -1;
    }
    inst->fd = fd;

    if (lirc_set_mode(inst) != 0) {
        close(fd);
        inst->fd = -1;
        return -1;
    }

    if (!inst->connected) {
        inst->connected = 1;
        inst->warned    = 0;
        fprintf(stderr, "[core:input:lirc] %s connected mode=%s%s%s (port=%d)\n",
                inst->device, lirc_mode_name(inst->mode),
                inst->mode == LIRC_MODE_MODE2 ? " protocol=" : "",
                inst->mode == LIRC_MODE_MODE2 ? inst->decoder->name : "", inst->port);
        if (inst->mode == LIRC_MODE_SCANCODE) lirc_warn_protocols(inst);
    }
    return 0;
}

static void lirc_disconnect(lirc_instance_t *inst)
{
    close(inst->fd);
    inst->fd     = -1;
    inst->warned = 0;
    inst->decoder->reset(inst->port);
    if (inst->connected) {
        inst->connected = 0;
        fprintf(stderr, "[core:input:lirc] %s disconnected (port=%d)\n",
                inst->device, inst->port);
    }
}

static int lirc_read_once(lirc_instance_t *inst)
{
    ssize_t got;

    if (inst->mode == LIRC_MODE_SCANCODE) {
        struct lirc_scancode sc;
        got = read(inst->fd, &sc, sizeof(sc));
        if (got == (ssize_t)sizeof(sc)) {
            lirc_feed_scancode(inst, &sc);
            return 0;
        }
    } else if (inst->mode == LIRC_MODE_MODE2) {
        uint32_t buf[128];
        got = read(inst->fd, buf, sizeof(buf));
        if (got > 0) {
            for (size_t i = 0; i < (size_t)got / sizeof(buf[0]); i++)
                lirc_mode2_feed(inst, buf[i]);
            return 0;
        }
    } else {
        /* same read as lircd: the bytes land in the low part of the integer */
        uint64_t raw = 0;
        got = read(inst->fd, &raw, inst->code_bytes);
        if (got == (ssize_t)inst->code_bytes) {
            lirc_emit(inst, (uint32_t)raw, false);
            return 0;
        }
    }

    if (got < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
    if (got > 0) return 0; /* short read: the kernel hands out whole records */
    return -1;
}

static void *lirc_thread(void *arg)
{
    lirc_instance_t *inst = (lirc_instance_t *)arg;

    while (inst->running) {
        if (inst->fd < 0) {
            if (lirc_try_connect(inst) != 0) {
                if (lirc_sleep(inst, inst->retry_ms) != 0) break;
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
            lirc_disconnect(inst);
            if (lirc_sleep(inst, inst->retry_ms) != 0) break;
            continue;
        }
        if (!(pfd[1].revents & POLLIN)) continue;

        if (lirc_read_once(inst) != 0) {
            lirc_disconnect(inst);
            if (lirc_sleep(inst, inst->retry_ms) != 0) break;
        }
    }

    if (inst->fd >= 0) {
        close(inst->fd);
        inst->fd = -1;
    }
    return NULL;
}

static bool lirc_open(int port, const char *searchparams)
{
    if (port < 0 || port >= LIRC_MAX_INSTANCES) {
        fprintf(stderr, "[core:input:lirc] invalid port %d\n", port);
        return false;
    }

    char device[256] = {0};
    if (!searchparams ||
        (!parse_param_str(searchparams, "dev", device, sizeof(device)) &&
         !parse_param_str(searchparams, "device", device, sizeof(device))))
        snprintf(device, sizeof(device), "/dev/lirc0");

    lirc_instance_t *inst = &g_instances[port];
    if (inst->running) return false;

    memset(inst, 0, sizeof(*inst));
    lirc_normalize_device(device, inst->device, sizeof(inst->device));
    inst->port      = port;
    inst->fd        = -1;
    inst->retry_ms  = parse_param_int(searchparams, "retry", 1000);
    inst->ttl_ms    = parse_param_int(searchparams, "ttl", 200);
    inst->repeat    = parse_param_int(searchparams, "repeat", 1) != 0;
    inst->debug     = parse_param_int(searchparams, "debug", 0) != 0;
    inst->dump      = parse_param_int(searchparams, "dump", 0) != 0;
    inst->decoder_flags = (inst->debug ? GAMELY_LIRC_DEBUG : 0u)
                        | (parse_param_int(searchparams, "strict", 0) ? GAMELY_LIRC_STRICT : 0u);
    inst->mask      = parse_param_hex(searchparams, "mask", 0xFFFFFFFFu);
    inst->want_mode = parse_param_mode(searchparams);
    inst->decoder   = parse_param_protocol(searchparams);
    if (!inst->decoder) return false;
    if (inst->retry_ms < 50) inst->retry_ms = 50;
    if (inst->ttl_ms < 1)    inst->ttl_ms = 1;  /* ttl=0 would latch the key down */
    if (inst->mask == 0)     inst->mask = 0xFFFFFFFFu;

    if (pipe(inst->wake) != 0) {
        fprintf(stderr, "[core:input:lirc] pipe failed (%s)\n", strerror(errno));
        return false;
    }

    inst->running = 1;
    if (pthread_create(&inst->thread, NULL, lirc_thread, inst) != 0) {
        fprintf(stderr, "[core:input:lirc] pthread_create failed\n");
        inst->running = 0;
        close(inst->wake[0]);
        close(inst->wake[1]);
        return false;
    }

    /* success even with the device missing: the thread keeps retrying */
    fprintf(stderr, "[core:input:lirc] %s mode=%s protocol=%s retry=%dms ttl=%dms mask=0x%08X repeat=%d (port=%d)\n",
            inst->device, lirc_mode_name(inst->want_mode), inst->decoder->name,
            inst->retry_ms, inst->ttl_ms, inst->mask, inst->repeat, port);
    return true;
}

static void lirc_close(int port)
{
    if (port < 0 || port >= LIRC_MAX_INSTANCES) return;
    lirc_instance_t *inst = &g_instances[port];
    if (!inst->running) return;

    inst->running = 0;
    if (write(inst->wake[1], "x", 1) < 0) { /* thread already gone */ }
    pthread_join(inst->thread, NULL);

    close(inst->wake[0]);
    close(inst->wake[1]);
    inst->wake[0] = inst->wake[1] = -1;
}

const gamely_input_driver_t gamely_driver_lirc = { lirc_open, lirc_close };
