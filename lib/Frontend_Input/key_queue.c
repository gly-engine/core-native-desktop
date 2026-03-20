#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

#include "gecnd.h"

#define GECND_INPUT_QUEUE_SIZE 128

typedef struct {
    char key[32];
    bool pressed;
} gecnd_key_event_t;

static gecnd_key_event_t queue[GECND_INPUT_QUEUE_SIZE];
static atomic_int head = 0;
static atomic_int tail = 0;
/* protects concurrent enqueue from multiple producer threads (main + aui) */
static pthread_mutex_t enqueue_mutex = PTHREAD_MUTEX_INITIALIZER;

void gecnd_set_btn_state(gecnd_t *gly, const char* key, bool state) {
    (void)gly;
    if (!key) return;

    pthread_mutex_lock(&enqueue_mutex);

    int current_head = atomic_load_explicit(&head, memory_order_relaxed);
    int next_head = (current_head + 1) % GECND_INPUT_QUEUE_SIZE;

    if (next_head != atomic_load_explicit(&tail, memory_order_acquire)) {
        strncpy(queue[current_head].key, key, sizeof(queue[current_head].key) - 1);
        queue[current_head].key[sizeof(queue[current_head].key) - 1] = '\0';
        queue[current_head].pressed = state;
        atomic_store_explicit(&head, next_head, memory_order_release);
    }

    pthread_mutex_unlock(&enqueue_mutex);
}

void gecnd_input_poll_events(gecnd_t *gly) {
    if (!gly) return;

    int current_tail = atomic_load_explicit(&tail, memory_order_relaxed);
    while (current_tail != atomic_load_explicit(&head, memory_order_acquire)) {
        gecnd_dispatch_key_event(gly, queue[current_tail].key, queue[current_tail].pressed);
        current_tail = (current_tail + 1) % GECND_INPUT_QUEUE_SIZE;
        atomic_store_explicit(&tail, current_tail, memory_order_release);
    }
}
