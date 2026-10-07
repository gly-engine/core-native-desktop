/**
 * @file plugins/libretro/include/netplay/common/session.h
 * @brief What a netplay session is on either side (client or host): the
 * players and the ports they control, every player's input by frame, and
 * the input the core sees while a frame runs.
 */
#ifndef GECND_NETPLAY_SESSION_H
#define GECND_NETPLAY_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

/** @brief Frames of input kept per player. */
#define NP_RING 256

/** @brief Most words of input a player sends per frame (4 analog pads). */
#define NP_MAX_WORDS 16

/** @brief This implementation's tag in the connection header. */
#define NP_IMPL_TAG 0x4743444Eu /* "GCDN" */

#ifdef GECND_NETPLAY_ZLIB
#define NP_COMPRESSION 1u  /* zlib */
#else
#define NP_COMPRESSION 0u
#endif

/** @brief What a session needs from the loaded core. */
typedef struct {
    const char *core_name;     /* retro_system_info.library_name */
    const char *core_version;  /* retro_system_info.library_version */
    uint32_t    content_crc;   /* CRC-32 of the content, 0 if unknown */
    size_t    (*serialize_size)(void);
    bool      (*serialize)(void *data, size_t size);
    bool      (*unserialize)(const void *data, size_t size);
    void     *(*memory_data)(unsigned id);
    size_t    (*memory_size)(unsigned id);
    void      (*reset)(void);
    void      (*set_port_device)(unsigned port, unsigned device);
    void      (*run)(void);
    /** @brief Local buttons of a local controller, bit n = joypad id n. */
    uint32_t  (*local_buttons)(unsigned local_port);
    /** @brief Frames run again after a misprediction: no video nor audio. */
    void      (*replay)(bool on);
} netplay_core_t;

typedef struct {
    uint32_t frame;
    bool     set;
    bool     guess;    /* predicted, the player's real input not here yet */
    uint32_t words[NP_MAX_WORDS];
} np_input_t;

typedef struct {
    uint32_t devices;  /* port bitmap */
    uint32_t from;     /* first frame with input */
    uint32_t until;    /* first frame without input */
    char     nick[NP_NICK_LEN];
} np_player_t;

/**
 * @brief The session: one at a time, client or host.
 *
 * Frames run as soon as their time comes, with a guess (the last real
 * input) for every player whose input is not here yet. Frames before
 * `confirmed` ran with real inputs only; when a real input differs from
 * the guess a frame ran with, the session goes back to that frame
 * (`rewind`) and runs again up to `self_frame` (rollback.h).
 */
typedef struct {
    uint32_t    devices[NP_MAX_DEVICES];  /* device type per port */
    np_player_t players[NP_MAX_CLIENTS];
    np_input_t  inputs[NP_MAX_CLIENTS][NP_RING];
    uint32_t    run_frame;                /* the frame running */
    uint32_t    self_frame;               /* next frame to run */
    uint32_t    confirmed;                /* every frame before it is final */
    uint32_t    rewind;                   /* first frame to run again, or NP_NO_FRAME */
    bool        reset_pending;            /* the core resets at reset_frame */
    uint32_t    reset_frame;
} np_session_t;

#define NP_NO_FRAME UINT32_MAX

extern np_session_t np_session;

/**
 * @brief Frames between reading a local controller and the frame its
 * input is for: more means fewer guesses gone wrong (rollbacks), less
 * means less delay. RetroArch calls it input latency frames.
 */
extern unsigned np_input_delay;

/** @brief Forgets every player and input. */
void np_session_reset(void);

/** @brief Words a player sends per frame for a port bitmap. */
unsigned np_words_for(uint32_t devices);

/** @brief Whether a player has input in a frame. */
bool np_player_at(uint32_t client, uint32_t frame);

/**
 * @brief Stores a player's real input for a frame, from big endian words;
 * one that differs from what a frame already ran with rewinds to it.
 */
void np_store_input(uint32_t client, uint32_t frame, const uint8_t *data, unsigned words);

/** @brief A player's real input for a frame, or NULL. */
const np_input_t *np_input(uint32_t client, uint32_t frame);

/** @brief Guesses the input of the players whose input for a frame is not
 * here: what each last sent. */
void np_guess_inputs(uint32_t frame);

/** @brief Whether every player in a frame has real input for it. */
bool np_inputs_ready(uint32_t frame);

/** @brief Runs again from a frame (a guess or a player turned out wrong). */
void np_session_rewind(uint32_t frame);

/**
 * @brief A player's input for a frame, read from the local controllers
 * (live) or idle, as big endian words; returns the payload size.
 */
unsigned np_local_input(uint32_t client, const netplay_core_t *core, bool live, uint8_t *out);

/** @brief The input the core sees during the running frame. */
int16_t np_session_input(unsigned port, unsigned device, unsigned index, unsigned id);

/** @brief The header's platform word: endianness and type sizes. */
uint32_t np_platform(void);

uint32_t np_rd32(const uint8_t *p);
void     np_wr32(uint8_t *p, uint32_t v);
bool     np_reserve(uint8_t **buf, size_t *cap, size_t need);

/** @brief CRC-32 (zlib's), as the content checksum netplay compares. */
uint32_t netplay_crc32(const void *data, size_t size);

#endif
