#include <stdbool.h>
#include <stdint.h>

//! @cond
typedef struct lua_State lua_State;
//! @endcond

//! @cond
#if defined(GLY_HOOK_IMPL) && !defined(GLY_EMPTY_HOOKS)
#define CREATE_GLY_HOOK_VOID(ret, name, args) \
    __attribute__((weak)) ret name args {}
#define CREATE_GLY_HOOK_BOOL(name, args) \
    __attribute__((weak)) bool name args { return false; }
#else
#define CREATE_GLY_HOOK_VOID(ret, name, args) ret name args;
#define CREATE_GLY_HOOK_BOOL(name, args) bool name args;
#endif
#define CREATE_GLY_HOOK(ret, name, args) CREATE_GLY_HOOK_VOID(ret, name, args)
//! @endcond

CREATE_GLY_HOOK(void, gly_hook_should_close, (bool*))
CREATE_GLY_HOOK(void, gly_hook_display_init, (uint16_t, uint16_t))
CREATE_GLY_HOOK(void, gly_hook_luaopen_http, (lua_State*))
CREATE_GLY_HOOK(void, gly_hook_luaopen_cjson, (lua_State*))
CREATE_GLY_HOOK(void, gly_hook_luaopen_base64, (lua_State*))
CREATE_GLY_HOOK(void, gly_hook_luaopen_storage, (lua_State*))
CREATE_GLY_HOOK(void, gly_hook_luaclose_storage, (lua_State*))
CREATE_GLY_HOOK(void, native_draw_start, ())
CREATE_GLY_HOOK(void, native_draw_flush, ())
CREATE_GLY_HOOK(void, native_draw_color, (uint32_t))
CREATE_GLY_HOOK(void, native_draw_clear, (uint32_t))
CREATE_GLY_HOOK(void, native_draw_rect, (uint8_t, int16_t, int16_t, int16_t, int16_t))
CREATE_GLY_HOOK(void, native_draw_line, (int16_t, int16_t, int16_t, int16_t))
CREATE_GLY_HOOK(void, native_text_print, (int16_t, int16_t, const char *))
CREATE_GLY_HOOK(void, native_text_mensure, (const char *, int16_t *, int16_t *))
CREATE_GLY_HOOK(void, native_text_font_size, (uint8_t))
CREATE_GLY_HOOK(void, native_text_font_name, (const char *))
CREATE_GLY_HOOK(void, native_text_font_default, (uint8_t))
CREATE_GLY_HOOK(void, native_text_font_previous, ())
CREATE_GLY_HOOK(void, native_image_draw, (int32_t, int16_t, int16_t))
CREATE_GLY_HOOK(void, native_image_load, (const char*, int32_t, bool *const))
CREATE_GLY_HOOK(void, native_image_mensure, (int32_t, int16_t *const, int16_t *const))
CREATE_GLY_HOOK_BOOL(gly_hook_input_get_next, (char**, bool*))
