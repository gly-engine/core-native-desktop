#include <stdbool.h>
#include <signal.h>

int gecnd_signal = 0;

static void gecnd_handler(int sig) {
    gecnd_signal = sig;
}

void gecnd_global_signal_init() {
    static bool is_initied = false;
    if (is_initied) return;

    signal(SIGINT, gecnd_handler);
}
