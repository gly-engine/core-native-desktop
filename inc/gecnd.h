#include <stdbool.h>
#include <stdint.h>

//! @cond
typedef struct lua_State lua_State;
//! @endcond

typedef enum { GECND_CONTROL_NONE = 0, GECND_CONTROL_FPS, GECND_CONTROL_WINDOW } gecnd_enum_control_mode_t;

typedef struct {
    lua_State *L;
    gecnd_enum_control_mode_t control_mode;
    int ref_native_callback_init;
    int ref_native_callback_loop;
    int ref_native_callback_draw;
    int ref_native_callback_keyboard;
} *gecnd_t;

gecnd_t gecnd_new();
void gecnd_lua_open_graphics(gecnd_t gly, lua_State *L);

void gecnd_set_game_file(gecnd_t gly, const char *const path, char *const file);
void gecnd_set_engine_file(gecnd_t gly, const char *const path, const char *const file);
void gecnd_set_control_mode(gecnd_t gly, gecnd_enum_control_mode_t mode);

uint8_t gecnd_native_callback_init(gecnd_t gly, int16_t width, int16_t height);
uint8_t gecnd_native_callback_loop(gecnd_t gly, int16_t delta_time);
uint8_t gecnd_native_callback_draw(gecnd_t gly);
uint8_t gecnd_native_callback_keyboard(gecnd_t gly, char *const key, bool pressed);

void gecnd_destroy(gecnd_t gly);

const char *const gecnd_utils_get_cwd(void);
const char *const gecnd_utils_get_exe_cwd(void);
