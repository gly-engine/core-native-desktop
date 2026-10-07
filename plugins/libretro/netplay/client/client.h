/**
 * @file plugins/libretro/netplay/client/client.h
 * @brief Netplay client: joins a RetroArch (or compatible) netplay host
 * and plays the loaded core in sync with it.
 *
 * The host is the clock and the judge: this client runs a frame only once
 * it has every player's input for it, so it never predicts and never
 * rewinds. The host predicts our input and rewinds when it arrives, as it
 * does for any RetroArch client. Joining takes the host's savestate.
 */
#ifndef GECND_NETPLAY_CLIENT_H
#define GECND_NETPLAY_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief What the client needs from the loaded core. */
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
} netplay_core_t;

/**
 * @brief Connects to a host. mitm_session is the relay session to join,
 * NULL for a direct connection. Returns false if it could not even start.
 */
bool netplay_client_start(const char *host, uint16_t port, const char *mitm_session,
                          const char *nick, const netplay_core_t *core);

/** @brief Leaves the session and closes the connection. */
void netplay_client_stop(void);

/** @brief Whether a session is running (connecting included). */
bool netplay_client_active(void);

/**
 * @brief One frontend tick in place of retro_run: network work, then as
 * many frames as the host's inputs allow (at most a few, to catch up).
 */
void netplay_client_tick(void);

/** @brief CRC-32 (zlib's), as the content checksum netplay compares. */
uint32_t netplay_crc32(const void *data, size_t size);

/** @brief The input the core sees during a netplay frame. */
int16_t netplay_client_input(unsigned port, unsigned device, unsigned index, unsigned id);

#endif
