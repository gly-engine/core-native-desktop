#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include "gebuffer.h"

static MediaFrame frames[2] = {0};
static atomic_int front_idx = 0;
static atomic_int update_counter = 0;

MediaFrame* gecnd_buffer_get_front(void) {
    return &frames[atomic_load(&front_idx)];
}

MediaFrame* gecnd_buffer_get_back(void) {
    return &frames[1 - atomic_load(&front_idx)];
}

void gecnd_buffer_swap(void) {
    atomic_store(&front_idx, 1 - atomic_load(&front_idx));
    gecnd_buffer_notify_update();
}

void gecnd_buffer_notify_update(void) {
    atomic_fetch_add(&update_counter, 1);
}

bool gecnd_buffer_check_update(atomic_int *local_counter) {
    int current = atomic_load(&update_counter);
    if (current != atomic_load(local_counter)) {
        atomic_store(local_counter, current);
        return true;
    }
    return false;
}

MediaFrame* gecnd_get_background_frame(void) {
    MediaFrame *f = &frames[atomic_load(&front_idx)];
    if (atomic_load(&f->ready)) return f;
    return NULL;
}

static void frame_resize(MediaFrame *f, int w, int h, int format) {
    if (f->width == w && f->height == h && f->format == format && f->data[0]) {
        return;
    }

    if (f->data[0]) {
        free(f->data[0]);
    }

    memset(f, 0, sizeof(MediaFrame));
    f->width = w;
    f->height = h;
    f->format = format;

    size_t total_size = 0;
    if (format == GECND_PIX_FMT_YUV420P) {
        f->linesize[0] = w;
        f->linesize[1] = w / 2;
        f->linesize[2] = w / 2;
        total_size = (size_t)(w * h) + (size_t)(w / 2 * h / 2) * 2;
    } else {
        int bpp = (format == GECND_PIX_FMT_RGB565) ? 2 : 4;
        f->linesize[0] = w * bpp;
        total_size = (size_t)(w * h * bpp);
    }

    f->data[0] = malloc(total_size);
    if (format == GECND_PIX_FMT_YUV420P) {
        f->data[1] = f->data[0] + (w * h);
        f->data[2] = f->data[1] + (w / 2 * h / 2);
    }
    
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
