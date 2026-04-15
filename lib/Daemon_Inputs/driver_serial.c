#include "gamely_input.h"
#include <stdio.h>

/* @todo implement serial port reading and call gamely_daemon_input_push() */

static bool serial_open(int port, const char *device)
{
    (void)port;
    if (!device) { fprintf(stderr, "[core:input:serial] device required\n"); return false; }
    fprintf(stderr, "[core:input:serial] not yet implemented (device=%s)\n", device);
    return false;
}

static void serial_close(int port) { (void)port; }

const gamely_input_driver_t gamely_driver_serial = { serial_open, serial_close };
