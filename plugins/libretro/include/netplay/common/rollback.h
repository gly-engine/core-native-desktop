/**
 * @file plugins/libretro/include/netplay/common/rollback.h
 * @brief Running frames ahead of the inputs, client and host alike.
 *
 * Every frame runs on its time with a guess for the inputs not here yet
 * (session.h); its savestate is kept first, NP_ROLLBACK frames of them.
 * A real input that differs from its guess takes the core back to that
 * frame's state and runs every frame since again, with video and audio
 * off. A frame is final (confirmed) once every input before it is real;
 * the session never gets further than NP_ROLLBACK frames past that.
 *
 * Sync checks: every final frame has a CRC of the game's memory (system
 * and video RAM; the state with saved pointers blanked if the core shows
 * no memory), which two of these frontends compare. RetroArch compares
 * the CRC of the whole state, which only means something for cores whose
 * states are the same in every process (no saved pointers).
 */
#ifndef GECND_NETPLAY_ROLLBACK_H
#define GECND_NETPLAY_ROLLBACK_H

#include "session.h"

/** @brief Frames run past the last final one, at most. */
#define NP_ROLLBACK 20

/** @brief A frame became final: its state at the start and its CRCs. */
typedef void (*np_final_cb_t)(uint32_t frame, uint32_t game_crc);

/**
 * @brief Starts running from a frame, with the core's state as its start
 * (just loaded or the session's first).
 */
bool np_rb_start(const netplay_core_t *core, uint32_t frame, np_final_cb_t on_final);

/** @brief Frees the kept states. */
void np_rb_stop(void);

/** @brief Whether a frame may run now (not too far past the final ones). */
bool np_rb_can_run(void);

/** @brief Runs the next frame, guessing the inputs not here. */
void np_rb_run(void);

/** @brief Runs again from the frame a wrong guess asks for, if any. */
void np_rb_resolve(void);

/** @brief Marks final the frames before `limit` whose inputs are all real. */
void np_rb_confirm(uint32_t limit);

/** @brief The final state of the last final frame (the start of
 * np_session.confirmed), to send to a client. */
bool np_rb_final_state(const uint8_t **data, size_t *size);

/** @brief CRC of a final frame's whole state, as RetroArch compares;
 * false if the frame is no longer kept. */
bool np_rb_state_crc(uint32_t frame, uint32_t *crc);

/** @brief Whether the core's states are the same in any process. */
bool np_rb_states_portable(void);

/** @brief Frames run again since the last call, and rollbacks. */
void np_rb_stats(unsigned *rollbacks, unsigned *replayed);

#endif
