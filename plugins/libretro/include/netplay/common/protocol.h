/**
 * @file plugins/libretro/include/netplay/common/protocol.h
 * @brief Wire format of the libretro netplay protocol (versions 5 and 6),
 * as RetroArch speaks it, so this frontend can join a RetroArch host.
 *
 * Written from the protocol description (commands, payloads, handshake
 * order) and the values every implementation must agree on; no code was
 * taken from RetroArch.
 *
 * Every command is a 32 bit identifier and a 32 bit payload size, both
 * big endian, followed by the payload. Integers in payloads are big
 * endian too.
 */
#ifndef GECND_NETPLAY_PROTOCOL_H
#define GECND_NETPLAY_PROTOCOL_H

#include <stdint.h>

/** @brief First word of the connection header ("RANP"), or of a refusal
 * from a full host ("FULL"). */
#define NP_MAGIC_RANP 0x52414E50u
#define NP_MAGIC_FULL 0x46554C4Cu

/** @brief What the lobby sends to check a room ("POKE"): a host answers
 * with its header and closes. */
#define NP_MAGIC_POKE 0x504F4B45u

/** @brief Protocol versions this client speaks. */
#define NP_PROTOCOL_LOW  5u
#define NP_PROTOCOL_HIGH 6u

/** @brief Fixed sizes. */
#define NP_NICK_LEN      32
#define NP_PASS_HASH_LEN 64
#define NP_MAX_DEVICES   16
#define NP_MAX_CLIENTS   32

/** @brief Connection header: 6 words. */
enum {
    NP_HDR_MAGIC = 0,     /* NP_MAGIC_RANP */
    NP_HDR_PLATFORM,      /* endianness and type sizes */
    NP_HDR_COMPRESSION,   /* supported compressions (bit 0: zlib) */
    NP_HDR_SALT,          /* host: password salt, 0 = none; client: highest protocol */
    NP_HDR_PROTOCOL,      /* host: chosen protocol; client: lowest protocol */
    NP_HDR_IMPL,          /* implementation tag (a mismatch only warns) */
    NP_HDR_WORDS
};

/** @brief Commands. */
enum {
    NP_CMD_ACK               = 0x0000,
    NP_CMD_NAK               = 0x0001,
    NP_CMD_DISCONNECT        = 0x0002,
    NP_CMD_INPUT             = 0x0003,
    NP_CMD_NOINPUT           = 0x0004,
    NP_CMD_NICK              = 0x0020,
    NP_CMD_PASSWORD          = 0x0021,
    NP_CMD_INFO              = 0x0022,
    NP_CMD_SYNC              = 0x0023,
    NP_CMD_SPECTATE          = 0x0024,
    NP_CMD_PLAY              = 0x0025,
    NP_CMD_MODE              = 0x0026,
    NP_CMD_MODE_REFUSED      = 0x0027,
    NP_CMD_CRC               = 0x0040,
    NP_CMD_REQUEST_SAVESTATE = 0x0041,
    NP_CMD_LOAD_SAVESTATE    = 0x0042,
    NP_CMD_PAUSE             = 0x0043,
    NP_CMD_RESUME            = 0x0044,
    NP_CMD_STALL             = 0x0045,
    NP_CMD_RESET             = 0x0046,
    NP_CMD_CHEATS            = 0x0047,
    NP_CMD_NETPACKET         = 0x0048,
    NP_CMD_CFG               = 0x0061,
    NP_CMD_CFG_ACK           = 0x0062,
    NP_CMD_PLAYER_CHAT       = 0x1000,
    NP_CMD_PING_REQUEST      = 0x1100,
    NP_CMD_PING_RESPONSE     = 0x1101,
    NP_CMD_SETTING_ALLOW_PAUSING        = 0x2000,
    NP_CMD_SETTING_INPUT_LATENCY_FRAMES = 0x2001
};

/**
 * @brief NETPACKET: a word, then the core's packet. From a client the word
 * says who the packet is for (0 the host, NP_PACKET_BROADCAST everyone);
 * from the host, who sent it (0 the host itself). Unlike every other
 * command, its size counts the packet only, not the word before it.
 */
#define NP_PACKET_BROADCAST 0xFFFFu

/** @brief Bytes a command's payload takes after its 8 byte head. */
static inline uint32_t np_payload_size(uint32_t cmd, uint32_t size) {
    return cmd == 0x0048 /* NP_CMD_NETPACKET */ ? size + 4 : size;
}

/** @brief SYNC: the high bit of the client number word means paused. */
#define NP_SYNC_PAUSED (1u << 31)

/** @brief MODE: flags in the high bits of the client number word. */
#define NP_MODE_YOU     (1u << 31)
#define NP_MODE_PLAYING (1u << 30)
#define NP_MODE_SLAVE   (1u << 29)

/** @brief Payload sizes. */
#define NP_INFO_SIZE (4 + NP_NICK_LEN + NP_NICK_LEN)            /* crc, core name, core version */
#define NP_SYNC_MIN  (4 + 4 + NP_MAX_DEVICES * 4 + NP_MAX_DEVICES + NP_MAX_DEVICES * 4 + NP_NICK_LEN)
#define NP_MODE_SIZE (4 + 4 + 4 + NP_MAX_DEVICES + NP_NICK_LEN)  /* frame, flags+client, devices, share, nick */

/** @brief Words of input per frame for a device type (RETRO_DEVICE_*). */
static inline unsigned np_device_words(unsigned device) {
    switch (device & 0xff) {
    case 1: return 1;  /* JOYPAD: buttons, bit n = RETRO_DEVICE_ID_JOYPAD n */
    case 2: return 2;  /* MOUSE */
    case 3: return 5;  /* KEYBOARD */
    case 4: return 2;  /* LIGHTGUN */
    case 5: return 3;  /* ANALOG: buttons, left Y|X, right Y|X */
    default: return 0;
    }
}

#endif
