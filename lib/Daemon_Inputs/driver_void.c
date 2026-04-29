#include "gecnd.h"

static bool void_open(int port, const char *searchparams)
{
    (void)port; (void)searchparams;
    return true;
}

static void void_close(int port) { (void)port; }

const gamely_input_driver_t gamely_driver_void = { void_open, void_close };
