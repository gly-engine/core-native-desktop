#ifndef GENATIVE_H
#define GENATIVE_H

#include <stdint.h>

/*
 * The native draw/text API is implemented in Frontend_Api as function
 * POINTERS (see GECND_NATIVE_STUB / GECND_NATIVE_DAEMON in gecnd.h): each slot
 * defaults to a stub/daemon and a backend overrides it through the registry
 * ("set" on backend_func:<name>, wired by the frontend "bind").
 *
 * Any caller OUTSIDE Frontend_Api must include this header so the call goes
 * THROUGH the pointer. Without it the compiler emits a direct call to the
 * symbol — which is the pointer's storage, not code — and jumps into data.
 *
 * NOTE: do not include this from a backend translation unit; backends define
 * these names as static functions, which would clash with the pointers below.
 */

extern void (*native_draw_start)(void);
extern void (*native_draw_color)(uint32_t);
extern void (*native_draw_clear)(uint32_t);
extern void (*native_draw_rect)(uint8_t, int16_t, int16_t, int16_t, int16_t, int16_t);
extern void (*native_draw_line)(int16_t, int16_t, int16_t, int16_t);

extern void (*native_text_print)(int16_t, int16_t, const char *);
extern void (*native_text_mensure)(const char *, int16_t *, int16_t *);
extern void (*native_text_font_size)(uint8_t);
extern void (*native_text_font_name)(const char *);
extern void (*native_text_font_default)(uint8_t);

/* native_draw_flush has no pointer/bind: it stays a plain backend function. */
void native_draw_flush(void);

#endif
