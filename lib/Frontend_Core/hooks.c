#include <stdint.h>

//! @cond
#if defined(GLY_HOOK_TEMPLATE)
#define CREATE_GLY_HOOK(ret, name, args) __attribute__((weak)) ret name args;
#else
#define CREATE_GLY_HOOK(ret, name, args) \
    __attribute__((weak)) ret name args {}
#endif
//! @endcond

CREATE_GLY_HOOK(void, gly_hook_display_init, (uint16_t, uint16_t))