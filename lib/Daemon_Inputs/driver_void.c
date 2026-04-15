#include "gamely_input.h"

static bool void_open(int port, const char *device)
{
    (void)port; (void)device;
    return true;
}

static void void_close(int port) { (void)port; }

const gamely_input_driver_t gamely_driver_void = { void_open, void_close };
