/**
 * @file gecnd_netplay.h
 * @brief Netplay rooms from inside a core: environment calls private to
 * the gecnd frontend (core-native-desktop's libretro plugin), so a core
 * can list the libretro lobby's rooms, host one or join one from its own
 * menus. Any other frontend (RetroArch) answers false to all of them.
 *
 * The frontend and every core that uses these calls carry the same copy
 * of this file.
 *
 * Requests (POST_LOBBY, CONNECT, DISCONNECT) are taken during retro_run
 * and carried out after it: a core with the netpacket interface sees
 * start() and stop() between frames, as with RetroArch.
 */
#ifndef GECND_NETPLAY_H
#define GECND_NETPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief The lobby's room list (struct gecnd_netplay_lobby *): fetched in
 * the background, so a core asks every frame until it is there. With
 * data NULL, only says whether the frontend has these calls.
 */
#define GECND_ENVIRONMENT_NETPLAY_GET_LOBBY  (0x20000 /* RETRO_ENVIRONMENT_PRIVATE */ | 0x4701)

/** @brief Hosts a room (const struct gecnd_netplay_room *). */
#define GECND_ENVIRONMENT_NETPLAY_POST_LOBBY (0x20000 | 0x4702)

/** @brief Joins a room (const struct gecnd_netplay_join *). */
#define GECND_ENVIRONMENT_NETPLAY_CONNECT    (0x20000 | 0x4703)

/** @brief Leaves the session, hosted or joined (data unused). */
#define GECND_ENVIRONMENT_NETPLAY_DISCONNECT (0x20000 | 0x4704)

enum gecnd_netplay_lobby_state
{
   GECND_NETPLAY_LOBBY_NONE = 0, /* never asked for */
   GECND_NETPLAY_LOBBY_LOADING,
   GECND_NETPLAY_LOBBY_READY,
   GECND_NETPLAY_LOBBY_FAILED
};

struct gecnd_netplay_lobby
{
   bool        refresh; /* in: fetch the list again */
   int         state;   /* out: enum gecnd_netplay_lobby_state */
   /* out: the lobby's /list as it came (a JSON array of {"fields": {...}},
    * the same RetroArch reads), valid until the next refresh */
   const char *json;
   size_t      size;
};

struct gecnd_netplay_room
{
   const char *game_name; /* listed as the room's game: the content's name */
   uint16_t    port;      /* 0: 55435 */
   bool        listed;    /* announced in the lobby */
   const char *relay;     /* relay handle (saopaulo, nyc, madrid,
                           * singapore), NULL or "" for none */
};

struct gecnd_netplay_join
{
   const char *host;
   uint16_t    port;
   const char *mitm_session; /* through a relay (host and port are the
                              * relay's), NULL or "" straight */
};

#endif
