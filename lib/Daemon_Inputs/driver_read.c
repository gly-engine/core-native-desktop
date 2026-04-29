#include <stdio.h>

#include "gecnd.h"
/* @todo implement generic device file reading and call gamely_daemon_input_push() */

static bool read_open(int port, const char *searchparams)
{
    (void)port;
    if (!searchparams) { fprintf(stderr, "[core:input:read] file= required\n"); return false; }
    fprintf(stderr, "[core:input:read] not yet implemented (%s)\n", searchparams);
    return false;
}

static void read_close(int port) { (void)port; }

const gamely_input_driver_t gamely_driver_read = { read_open, read_close };
