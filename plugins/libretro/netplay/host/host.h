/**
 * @file plugins/libretro/netplay/host/host.h
 * @brief Netplay host: RetroArch (or compatible) clients join the game
 * running here.
 *
 * The host is the session's clock: it runs a frame once it has the input
 * of every player in it, sends its own input for the frame and forwards
 * everyone else's. RetroArch clients predict and rewind on their side,
 * so they keep playing smoothly while their input travels.
 */
#ifndef GECND_NETPLAY_HOST_H
#define GECND_NETPLAY_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "../common/session.h"

/** @brief Opens the session on a TCP port; the host is player 1. */
bool netplay_host_start(uint16_t port, const char *nick, const netplay_core_t *core);

/** @brief Closes every connection and the session. */
void netplay_host_stop(void);

/** @brief Whether a session is hosted here. */
bool netplay_host_active(void);

/** @brief One frontend tick in place of retro_run. */
void netplay_host_tick(void);

#endif
