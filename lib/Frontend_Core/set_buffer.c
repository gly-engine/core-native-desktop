#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include "gemedia.h"

static MediaFrame frames[2] = {0};
static atomic_int front_idx = 0;

MediaFrame* gecnd_buffer_get_front(void) {
    return &frames[atomic_load(&front_idx)];
}

MediaFrame* gecnd_buffer_get_back(void) {
    return &frames[1 - atomic_load(&front_idx)];
}

void gecnd_buffer_swap(void) {
    atomic_store(&front_idx, 1 - atomic_load(&front_idx));
}

static void frame_resize(MediaFrame *f, int w, int h, int format) {
    if (f->width == w && f->height == h && f->format == format && f->data[0]) {
        return;
    }

    if (f->data[0]) {
        free(f->data[0]);
    }

    f->width = w;
    f->height = h;
    f->format = format;

    // Simplified BPP for POC
    int bpp = (format == GE_PIX_FMT_RGB565) ? 2 : 4;
    f->data[0] = malloc(w * h * bpp);
    f->linesize[0] = w * bpp;
    
    atomic_store(&f->ready, false);
}

void gecnd_buffer_resize(int w, int h, int format) {
    frame_resize(&frames[0], w, h, format);
    frame_resize(&frames[1], w, h, format);
}

void gecnd_buffer_free(void) {
    if (frames[0].data[0]) free(frames[0].data[0]);
    if (frames[1].data[0]) free(frames[1].data[0]);
    memset(frames, 0, sizeof(frames));
}
