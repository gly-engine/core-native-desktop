#ifndef GAMELY_DRIVER_LINUX_LIRC_H
#define GAMELY_DRIVER_LINUX_LIRC_H

#include <stdbool.h>
#include <stdint.h>

/* concurrent lirc driver ports; also indexes the decoders' state */
#define LIRC_MAX_INSTANCES 4

/*
 * IR protocol decoder on top of LIRC_MODE_MODE2.
 *
 * the driver reads the pulse train from /dev/lircN and hands every raw
 * entry (LIRC_MODE2_* tag | duration in us) to the feed of the protocol
 * picked by ?protocol=. a decoder only reports facts — the scancode and
 * whether it came from a repeat frame. policy (?repeat=, ?mask=, ttl,
 * debug) stays in the driver.
 *
 * state is per port, indexed by port: no decoder allocates anything.
 */
#define GAMELY_LIRC_DEBUG  (1u << 0)   /* decoder may log diagnostics */
#define GAMELY_LIRC_STRICT (1u << 1)   /* drop frames failing the checksum */

typedef void (*gamely_lirc_emit_t)(void *usr, uint32_t scancode, bool repeat);

typedef struct {
    const char *name;
    void (*reset)(int port);
    void (*feed) (int port, uint32_t entry, unsigned flags,
                  gamely_lirc_emit_t emit, void *usr);
} gamely_lirc_decoder_t;

extern const gamely_lirc_decoder_t gamely_lirc_decoder_nec;

#endif /* GAMELY_DRIVER_LINUX_LIRC_H */
