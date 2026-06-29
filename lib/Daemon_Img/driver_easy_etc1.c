#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

/* PKM (ETC1 container) → ETC1 passthrough.
 *
 * Layout (16-byte header, big-endian):
 *   [0..5]  magic "PKM 10"
 *   [6..7]  data type (always 0 for ETC1)
 *   [8..9]  extended (padded) width   — multiple of 4
 *  [10..11] extended (padded) height  — multiple of 4
 *  [12..13] original width            — visible
 *  [14..15] original height           — visible
 *   [16..]  block payload, (ext_w/4)*(ext_h/4) * 8 bytes
 *
 * Zero-copy: pixels alias the payload inside `data`; GECND_FLAG_IMG_MOVE makes
 * the daemon keep `data` alive until unload instead of freeing it after decode. */
static gamely_img_decoded_t driver_decoder_etc1(const uint8_t *data, size_t len) {
    gamely_img_decoded_t out = {0};
    if (!data || len < 16) return out;
    if (memcmp(data, "PKM 10", 6) != 0) return out;

    uint16_t ext_w  = ((uint16_t)data[ 8] << 8) | data[ 9];
    uint16_t ext_h  = ((uint16_t)data[10] << 8) | data[11];
    uint16_t orig_w = ((uint16_t)data[12] << 8) | data[13];
    uint16_t orig_h = ((uint16_t)data[14] << 8) | data[15];

    if (orig_w == 0 || orig_h == 0)                return out;
    if (ext_w < orig_w || ext_h < orig_h)          return out;
    if ((ext_w & 3) || (ext_h & 3))                return out;

    size_t payload = ((size_t)ext_w / 4) * ((size_t)ext_h / 4) * 8;
    if (len < 16 + payload)                        return out;

    out.pixels       = (uint8_t *)(data + 16);
    out.len          = payload;
    out.w            = (int16_t)orig_w;
    out.h            = (int16_t)orig_h;
    out.color_format = GECND_PIX_FMT_ETC1;
    out.flags        = GECND_FLAG_IMG_MOVE;
    return out;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_decoder_sync:pkm:etc1", driver_decoder_etc1, NULL);
    gecnd_registry("set", "image_decoder_sync:etc1:etc1", driver_decoder_etc1, NULL);
}
