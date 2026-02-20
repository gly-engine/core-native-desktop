#ifndef GECND_BUFFER_H
#define GECND_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef enum {
    GECND_PIX_FMT_RGBA8888 = 0,
    GECND_PIX_FMT_YUV420P = 1,
    GECND_PIX_FMT_RGB565 = 2,
    GECND_PIX_FMT_NONE = -1
} GECNDColorFormat;

typedef struct {
    uint8_t *data[4];
    int linesize[4];
    int width;
    int height;
    int format; // Uses GECNDColorFormat
    double pts;
    atomic_bool ready;
} MediaFrame;

// Shared background media frame with double buffering (Managed by Frontend_Core/set_buffer.c)
MediaFrame* gecnd_buffer_get_front(void);
MediaFrame* gecnd_buffer_get_back(void);
void gecnd_buffer_swap(void);
void gecnd_buffer_notify_update(void);
bool gecnd_buffer_check_update(atomic_int *local_counter);
void gecnd_buffer_resize(int w, int h, int format);
void gecnd_buffer_free(void);

// Main background frame getter
MediaFrame* gecnd_get_background_frame(void);

#endif
