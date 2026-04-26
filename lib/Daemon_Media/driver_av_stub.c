#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

bool encode_init         (int w, int h, int fps, void (*on_ts)(const uint8_t *, int, int64_t)) {}
void encode_push         (const uint8_t *rgba, int w, int h) {}
void encode_shutdown     (void) {}
void encode_force_idr    (void) {}
int  encode_get_idr_cache(const uint8_t **out) {}

bool av_load_ffmpeg(void) {
    fprintf(stderr, "[media] FFmpeg not available in this build\n");
    return false;
}
