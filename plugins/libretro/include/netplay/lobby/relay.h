/**
 * @file plugins/libretro/include/netplay/lobby/relay.h
 * @brief The libretro relay (tunnel server): reaching a netplay host that
 * no client can connect to, through a server both can reach.
 *
 * Every relay message starts with an id: a magic and 12 unique bytes.
 *
 *   host:   connects and sends RATS with the unique bytes zeroed; the
 *           relay answers RATS with the session's unique bytes, announced
 *           in the lobby in base64. That connection stays open (control).
 *   relay:  RATL + id on the control connection when a client arrives;
 *           the host opens a new connection, asks the client's address
 *           with RATA + id on the control (the relay answers RATA + id +
 *           16 bytes of address), then sends RATL + id on the new
 *           connection, which from then on is that client's netplay.
 *   relay:  RATP (4 bytes) on the control connection: a ping to echo.
 *   client: connects and sends RATS + the session's unique bytes, then
 *           speaks netplay as with any host.
 */
#ifndef GECND_NETPLAY_RELAY_H
#define GECND_NETPLAY_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NP_RELAY_SESSION 0x52415453u /* RATS */
#define NP_RELAY_LINK    0x5241544Cu /* RATL */
#define NP_RELAY_ADDR    0x52415441u /* RATA */
#define NP_RELAY_PING    0x52415450u /* RATP */

/** @brief An id: magic and unique bytes, as sent (16 bytes). */
#define NP_RELAY_ID_SIZE     16
#define NP_RELAY_UNIQUE_SIZE 12
#define NP_RELAY_ADDR_SIZE   16

/** @brief Writes an id: magic (big endian) and unique bytes (NULL = 0). */
void np_relay_id(uint8_t out[NP_RELAY_ID_SIZE], uint32_t magic, const uint8_t *unique);

/** @brief Unique bytes in base64, as the lobby lists a session. */
void np_relay_encode(const uint8_t unique[NP_RELAY_UNIQUE_SIZE], char out[17]);

/** @brief The unique bytes of a session in base64; false if malformed. */
bool np_relay_decode(const char *text, uint8_t unique[NP_RELAY_UNIQUE_SIZE]);

/** @brief A client's connection through the relay, ready for netplay. */
typedef void (*netplay_relay_link_cb_t)(int fd, void *user);

/** @brief The relay gave the session (handle and base64 id) to list. */
typedef void (*netplay_relay_session_cb_t)(const char *handle, const char *session, void *user);

/**
 * @brief Hosts through a relay (nyc, madrid, saopaulo, singapore): asks
 * the lobby where it is, opens the control connection and takes a
 * session; then hands every client that arrives over to on_link.
 */
bool netplay_relay_host(const char *handle, netplay_relay_link_cb_t on_link,
                        netplay_relay_session_cb_t on_session, void *user);

/** @brief Relay work: call every tick while hosting. */
void netplay_relay_tick(void);

/** @brief Closes the session and every link still opening. */
void netplay_relay_stop(void);

/** @brief Whether a relay is wanted, and its session once given (or NULL). */
bool        netplay_relay_wanted(void);
const char *netplay_relay_session(void);
const char *netplay_relay_handle(void);

#endif
