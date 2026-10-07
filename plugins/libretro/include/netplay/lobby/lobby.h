/**
 * @file plugins/libretro/include/netplay/lobby/lobby.h
 * @brief Announces a hosted netplay room in the libretro lobby
 * (lobby.libretro.com), where RetroArch lists it to join.
 *
 * The room is announced when hosting starts and again every
 * NP_LOBBY_PERIOD seconds, with the current player count; the lobby drops
 * rooms that stop announcing. The lobby checks a room by connecting to it
 * and sending POKE, which the host answers with its header: this host
 * speaks the protocol, so the room shows as a RetroArch compatible one.
 */
#ifndef GECND_NETPLAY_LOBBY_H
#define GECND_NETPLAY_LOBBY_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Seconds between two announces (RetroArch uses 20). */
#define NP_LOBBY_PERIOD 20

/** @brief What the lobby shows of a room. */
typedef struct {
    const char *nick;          /* the host's player name */
    const char *core_name;
    const char *core_version;
    const char *game_name;     /* content name, without extension */
    uint32_t    game_crc;
    uint16_t    port;
} netplay_lobby_room_t;

/** @brief Starts announcing a room. */
void netplay_lobby_start(const netplay_lobby_room_t *room);

/** @brief Announces again when it is time, with the players now in. */
void netplay_lobby_tick(unsigned players, unsigned spectators);

/** @brief Stops announcing (the lobby drops the room by itself). */
void netplay_lobby_stop(void);

#endif
