#ifndef GECND_BUFFER_H
#define GECND_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef enum {
    GECND_PIX_FMT_RGBA8888 = 0,
    GECND_PIX_FMT_YUV420P  = 1,
    GECND_PIX_FMT_RGB565   = 2,
    GECND_PIX_FMT_NONE     = -1
} GECNDColorFormat;

typedef struct {
    uint8_t    *data[4];
    int         linesize[4];
    int         width;
    int         height;
    int         format;
    double      pts;
    atomic_bool ready;
} MediaFrame;

#endif
