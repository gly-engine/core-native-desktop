/**
 * @file plugins/libretro/source/netplay/common/rollback.c
 * @brief Frames run ahead of the inputs, kept states, rollbacks and the
 * CRCs of final frames (rollback.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netplay/common/rollback.h"

/** @brief States kept: the frames that can still go back, and one more. */
#define NP_STATES (NP_ROLLBACK + 2)

/** @brief RETRO_MEMORY_SYSTEM_RAM and RETRO_MEMORY_VIDEO_RAM. */
#define NP_MEMORY_SYSTEM_RAM 2
#define NP_MEMORY_VIDEO_RAM  3

typedef struct {
    uint32_t frame;
    bool     set;
    uint32_t game_crc;
    uint8_t *data;
} np_slot_t;

static struct {
    bool           active;
    netplay_core_t core;
    np_final_cb_t  on_final;
    size_t         size;
    np_slot_t      slots[NP_STATES];
    uint8_t       *scratch;
    bool           portable;
    unsigned       rollbacks, replayed;
} rb;

/**
 * @brief Whether 8 bytes of a state look like an address of this process
 * (Linux on 64 bits maps the heap and libraries at 0x55.. to 0x7f..): a
 * pointer the core saved as is, which differs in every process.
 */
static bool rb_pointer_at(const uint8_t *state, size_t at) {
    uint64_t v;
    if (sizeof(void *) != 8) return false;
    memcpy(&v, state + at, 8);
    return v >> 40 >= 0x55 && v >> 40 <= 0x7f;
}

/** @brief CRC of the game: its memory, or its state without pointers. */
static uint32_t rb_game_crc(const uint8_t *state) {
    static const unsigned ids[] = { NP_MEMORY_SYSTEM_RAM, NP_MEMORY_VIDEO_RAM };
    uint32_t crc = 0;
    bool any = false;

    if (rb.core.memory_data && rb.core.memory_size)
        for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
            const void *data = rb.core.memory_data(ids[i]);
            const size_t size = rb.core.memory_size(ids[i]);
            if (!data || !size) continue;
            crc = crc * 0x01000193u ^ netplay_crc32(data, size);
            any = true;
        }
    if (any) return crc;

    memcpy(rb.scratch, state, rb.size);
    for (size_t at = 0; at + 8 <= rb.size; at += 8)
        if (rb_pointer_at(rb.scratch, at)) memset(rb.scratch + at, 0, 8);
    return netplay_crc32(rb.scratch, rb.size);
}

/** @brief Keeps the state at the start of a frame. */
static np_slot_t *rb_save(uint32_t frame) {
    np_slot_t *slot = &rb.slots[frame % NP_STATES];

    slot->set = rb.core.serialize(slot->data, rb.size);
    if (!slot->set) return NULL;
    slot->frame = frame;
    slot->game_crc = rb_game_crc(slot->data);
    return slot;
}

static const np_slot_t *rb_slot(uint32_t frame) {
    const np_slot_t *slot = &rb.slots[frame % NP_STATES];
    return slot->set && slot->frame == frame ? slot : NULL;
}

/**
 * @brief Runs a frame: its state kept, the missing inputs guessed.
 *
 * The frame always starts from the kept state loaded back: a core's state
 * does not hold every byte of the core (timing, sound), and a frame run
 * from a loaded state may differ from one run straight on. Loading it
 * every time makes a frame run again after a wrong guess, and the same
 * frame on every other peer, start alike.
 */
static void rb_frame(uint32_t frame) {
    const np_slot_t *slot = rb_save(frame);

    if (slot) rb.core.unserialize(slot->data, rb.size);

    if (slot && frame == np_session.confirmed && rb.on_final)
        rb.on_final(frame, slot->game_crc);
    np_guess_inputs(frame);
    if (np_session.reset_pending && np_session.reset_frame == frame && rb.core.reset)
        rb.core.reset();
    np_session.run_frame = frame;
    rb.core.run();
}

bool np_rb_start(const netplay_core_t *core, uint32_t frame, np_final_cb_t on_final) {
    const size_t size = core->serialize_size ? core->serialize_size() : 0;

    if (!size || !core->serialize || !core->unserialize) {
        fprintf(stderr, "[netplay] the core cannot make savestates: no netplay with it\n");
        return false;
    }
    if (size != rb.size) {
        np_rb_stop();
        for (unsigned i = 0; i < NP_STATES; i++)
            if (!(rb.slots[i].data = malloc(size))) {
                np_rb_stop();
                return false;
            }
        if (!(rb.scratch = malloc(size))) {
            np_rb_stop();
            return false;
        }
        rb.size = size;
    }
    rb.core = *core;
    rb.on_final = on_final;
    rb.active = true;
    for (unsigned i = 0; i < NP_STATES; i++) rb.slots[i].set = false;

    /* portable states: no pointers, and the same after a round trip */
    rb.portable = false;
    if (rb.core.serialize(rb.scratch, size)) {
        bool pointers = false;
        for (size_t at = 0; at + 8 <= size && !pointers; at += 8)
            pointers = rb_pointer_at(rb.scratch, at);
        rb.portable = !pointers && rb.core.unserialize(rb.scratch, size) &&
                      rb.core.serialize(rb.slots[0].data, size) &&
                      !memcmp(rb.scratch, rb.slots[0].data, size);
    }

    np_session.self_frame = np_session.confirmed = frame;
    np_session.rewind = NP_NO_FRAME;
    return true;
}

void np_rb_stop(void) {
    for (unsigned i = 0; i < NP_STATES; i++) {
        free(rb.slots[i].data);
        rb.slots[i].data = NULL;
        rb.slots[i].set = false;
    }
    free(rb.scratch);
    rb.scratch = NULL;
    rb.size = 0;
    rb.active = false;
}

bool np_rb_can_run(void) {
    return rb.active && np_session.self_frame - np_session.confirmed < NP_ROLLBACK;
}

void np_rb_run(void) {
    rb_frame(np_session.self_frame);
    np_session.self_frame++;
}

void np_rb_resolve(void) {
    const uint32_t from = np_session.rewind;
    const uint32_t to = np_session.self_frame;
    const np_slot_t *slot;

    if (from == NP_NO_FRAME) return;
    np_session.rewind = NP_NO_FRAME;
    if (!rb.active || from >= to) return;
    if (!(slot = rb_slot(from)) || !rb.core.unserialize(slot->data, rb.size)) {
        fprintf(stderr, "[netplay] cannot go back to frame %u (at %u): out of sync\n", from, to);
        return;
    }
    if (rb.core.replay) rb.core.replay(true);
    for (uint32_t f = from; f < to; f++) rb_frame(f);
    if (rb.core.replay) rb.core.replay(false);
    rb.rollbacks++;
    rb.replayed += to - from;
}

void np_rb_confirm(uint32_t limit) {
    while (np_session.confirmed < limit && np_session.confirmed < np_session.self_frame &&
           np_session.confirmed < np_session.rewind && np_inputs_ready(np_session.confirmed)) {
        const np_slot_t *slot;

        np_session.confirmed++;
        if (np_session.reset_pending && np_session.reset_frame < np_session.confirmed)
            np_session.reset_pending = false;
        /* the frame now final already ran: its state is kept (otherwise it
         * is final when it runs) */
        if (np_session.confirmed < np_session.self_frame &&
            (slot = rb_slot(np_session.confirmed)) && rb.on_final)
            rb.on_final(np_session.confirmed, slot->game_crc);
    }
}

bool np_rb_final_state(const uint8_t **data, size_t *size) {
    const np_slot_t *slot;

    if (!rb.active) return false;
    if (np_session.confirmed == np_session.self_frame) {
        if (!rb.core.serialize(rb.scratch, rb.size)) return false;
        *data = rb.scratch;
    } else if ((slot = rb_slot(np_session.confirmed))) {
        *data = slot->data;
    } else {
        return false;
    }
    *size = rb.size;
    return true;
}

bool np_rb_state_crc(uint32_t frame, uint32_t *crc) {
    const np_slot_t *slot;

    if (!rb.active || frame > np_session.confirmed) return false;
    if (frame == np_session.self_frame) {
        if (!rb.core.serialize(rb.scratch, rb.size)) return false;
        *crc = netplay_crc32(rb.scratch, rb.size);
        return true;
    }
    if (!(slot = rb_slot(frame))) return false;
    *crc = netplay_crc32(slot->data, rb.size);
    return true;
}

bool np_rb_states_portable(void) {
    return rb.portable;
}

void np_rb_stats(unsigned *rollbacks, unsigned *replayed) {
    *rollbacks = rb.rollbacks;
    *replayed = rb.replayed;
    rb.rollbacks = rb.replayed = 0;
}
