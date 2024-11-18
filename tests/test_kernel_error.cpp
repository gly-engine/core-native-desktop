#include <cassert>

extern "C" {
#include "zeebo.h"
}

int main()
{
    // no errors
    assert(kernel_has_error() == false);
    assert(strncmp(kernel_get_error(), "", 1024) == 0);

    // has errors
    kernel_add_error("pane no sistema,");
    kernel_add_error("alguem me desconfigurou!");
    assert(kernel_has_error() == true);
    assert(strcmp(kernel_get_error(), "pane no sistema,\nalguem me desconfigurou!\n") == 0);

    return 0;
}
