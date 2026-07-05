#include <stdbool.h>
#include <stdint.h>

//! @cond
typedef struct lua_State lua_State;
//! @endcond

//! @cond
#if defined(GLY_HOOK_IMPL) && !defined(GLY_EMPTY_HOOKS)
#define CREATE_GLY_HOOK(ret, name, args) \
    __attribute__((weak)) ret name args {}
#else
#define CREATE_GLY_HOOK(ret, name, args) ret name args;
#endif
//! @endcond

CREATE_GLY_HOOK(void, gly_hook_should_close, (bool*))
CREATE_GLY_HOOK(void, gly_hook_display_init, (uint16_t, uint16_t))
CREATE_GLY_HOOK(void, gly_hook_display_fps, (uint8_t))
CREATE_GLY_HOOK(void, gly_hook_display_dt, (int16_t*))
