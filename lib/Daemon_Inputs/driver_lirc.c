#include "gecnd.h"
#include <stdio.h>

/* @todo implement LIRC device reading and call gamely_daemon_input_push() */

static bool lirc_open(int port, const char *device)
{
    (void)port;
    if (!device) { fprintf(stderr, "[core:input:lirc] device required\n"); return false; }
    fprintf(stderr, "[core:input:lirc] not yet implemented (device=%s)\n", device);
    return false;
}

static void lirc_close(int port) { (void)port; }

const gamely_input_driver_t gamely_driver_lirc = { lirc_open, lirc_close };
