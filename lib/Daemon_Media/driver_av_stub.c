#include <stdio.h>
#include <stdbool.h>

#define GECND_STREAM_AVLIB_INTERNAL
#define GECND_FFMPEG_LOAD_INTERNAL
#include "gemedia.h"

av_api AV;

bool av_load_ffmpeg(void) {
    fprintf(stderr, "[media] FFmpeg not available in this build\n");
    return false;
}
