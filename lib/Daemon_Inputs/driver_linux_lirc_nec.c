#include <stdio.h>
#include <string.h>

#include <linux/lirc.h>

#include "driver_linux_lirc.h"

/*
 * NEC over MODE2. frame: 9ms + 4.5ms header, 32 bits (addr, ~addr, cmd,
 * ~cmd) LSB first, stop pulse, gap. repeat frame: 9ms + 2.25ms + pulse,
 * carries no data — resends the last code.
 *
 * all the information lives in the SPACE durations; the 560us pulses are
 * just separators. decoding from spaces alone costs nothing and tolerates
 * a receiver that reports skewed pulses or drops entries.
 */

/* in us. wide tolerance: a cheap receiver easily gets the length wrong */
#define NEC_HDR_PULSE_MIN   6300
#define NEC_HDR_PULSE_MAX  11700
#define NEC_HDR_SPACE_MIN   3500
#define NEC_HDR_SPACE_MAX   5500
#define NEC_RPT_SPACE_MIN   1800
#define NEC_RPT_SPACE_MAX   2800
#define NEC_BIT_SPACE_MAX   2400
#define NEC_BIT_ONE_MIN     1120   /* halfway between 560 (0) and 1690 (1) */

/*
 * a NEC repeat arrives every ~108ms while the button is held, separated
 * from the previous frame by a 40-96ms gap. a "repeat" after a long
 * silence is not a repeat, it is noise that happened to match the shape —
 * re-emitting on that holds the key down forever, since every emission
 * renews the ttl.
 *
 * the gap measured is the SPACE right before the header, not an
 * accumulator: a receiver reports idle as a saturated space (0xFFFFFF =
 * 16.7s) and summing that would blow past the limit, dropping legitimate
 * repeats and making the key flicker. 250ms of slack: twice the real
 * worst case.
 */
#define NEC_RPT_MAX_GAP_US 250000

enum { NEC_IDLE = 0, NEC_HDR_SPACE, NEC_BITS };

typedef struct {
    int      state;
    int      bit;
    uint32_t bits;
    uint32_t last;
    int      have_last;
    uint32_t last_space; /* last space seen */
    uint32_t gap_us;     /* space preceding the current header */
} nec_state_t;

static nec_state_t g_nec[LIRC_MAX_INSTANCES];

static void nec_reset(int port)
{
    if (port < 0 || port >= LIRC_MAX_INSTANCES) return;
    g_nec[port].state = NEC_IDLE;
    g_nec[port].bit   = 0;
    g_nec[port].bits  = 0;
}

/*
 * same conversion as the kernel's ir_nec_bytes_to_scancode(), so a keymap
 * written against ir-keytable/scancode keeps working here.
 */
static uint32_t nec_scancode(uint32_t bits)
{
    uint8_t address     = (uint8_t)( bits        & 0xFF);
    uint8_t not_address = (uint8_t)((bits >>  8) & 0xFF);
    uint8_t command     = (uint8_t)((bits >> 16) & 0xFF);
    uint8_t not_command = (uint8_t)((bits >> 24) & 0xFF);

    if ((uint8_t)(command ^ not_command) != 0xFF)  /* NEC32 (Apple, TiVo...) */
        return (uint32_t)not_address << 24 | (uint32_t)address << 16 |
               (uint32_t)not_command <<  8 | (uint32_t)command;
    if ((uint8_t)(address ^ not_address) != 0xFF)  /* extended NEC */
        return (uint32_t)address << 16 | (uint32_t)not_address << 8 | command;
    return (uint32_t)address << 8 | command;       /* plain NEC */
}

static void nec_feed(int port, uint32_t entry, unsigned flags,
                     gamely_lirc_emit_t emit, void *usr)
{
    uint32_t tag = LIRC_MODE2(entry);
    uint32_t val = LIRC_VALUE(entry);
    nec_state_t *st;

    if (port < 0 || port >= LIRC_MAX_INSTANCES) return;
    st = &g_nec[port];

    if (tag == LIRC_MODE2_FREQUENCY) return;

    if (tag == LIRC_MODE2_TIMEOUT || tag == LIRC_MODE2_OVERFLOW) {
        st->last_space = val > LIRC_VALUE_MASK ? LIRC_VALUE_MASK : val;
        nec_reset(port);
        return;
    }

    if (tag == LIRC_MODE2_PULSE) {
        if (val >= NEC_HDR_PULSE_MIN && val <= NEC_HDR_PULSE_MAX) {
            uint32_t gap = st->last_space;
            nec_reset(port);
            st->gap_us = gap;
            st->state  = NEC_HDR_SPACE;
        } else if (val > NEC_HDR_PULSE_MAX) {
            nec_reset(port);  /* pulse too long to be NEC */
        }
        return;               /* bit pulse: ignored on purpose */
    }

    st->last_space = val;

    switch (st->state) {
    case NEC_HDR_SPACE:
        if (val >= NEC_HDR_SPACE_MIN && val <= NEC_HDR_SPACE_MAX) {
            st->state = NEC_BITS;
        } else if (val >= NEC_RPT_SPACE_MIN && val <= NEC_RPT_SPACE_MAX) {
            if (st->have_last && st->gap_us <= NEC_RPT_MAX_GAP_US) {
                emit(usr, st->last, true);
            } else if (st->have_last && (flags & GAMELY_LIRC_DEBUG)) {
                fprintf(stderr, "[core:debug:input] nec repeat dropped gap= %uus\n",
                        st->gap_us);
            }
            nec_reset(port);
        } else {
            nec_reset(port);
        }
        break;

    case NEC_BITS:
        if (val > NEC_BIT_SPACE_MAX) {  /* gap: frame came out incomplete */
            nec_reset(port);
            break;
        }
        if (val >= NEC_BIT_ONE_MIN)
            st->bits |= 1u << st->bit;
        if (++st->bit == 32) {
            uint32_t bits = st->bits;
            /* NEC sends cmd and ~cmd: a frame that does not check out is
             * either a real NEC32 (Apple, TiVo) or garbage — pulse train
             * cut in half, fast button switch, noise. ?strict=1 drops it. */
            bool ok = ((uint8_t)((bits >> 16) ^ (bits >> 24)) == 0xFF);
            nec_reset(port);
            if (!ok && (flags & GAMELY_LIRC_DEBUG))
                fprintf(stderr, "[core:debug:input] nec checksum fail raw= 0x%08X%s\n",
                        bits, (flags & GAMELY_LIRC_STRICT) ? " (dropped)" : "");
            if (!ok && (flags & GAMELY_LIRC_STRICT)) break;
            st->last      = nec_scancode(bits);
            st->have_last = 1;
            emit(usr, st->last, false);
        }
        break;

    default:
        break;
    }
}

const gamely_lirc_decoder_t gamely_lirc_decoder_nec = { "nec", nec_reset, nec_feed };
