#include <stdbool.h>
#include <stdint.h>

//! @cond
typedef struct lua_State lua_State;
//! @endcond

typedef enum {
    GECND_CONTROL_NONE = 0, 
    GECND_CONTROL_FPS = 1,
    GECND_CONTROL_WINDOW = 2
} gecnd_enum_control_mode_t;

typedef enum {
    GECND_OK = 0,
    GECND_ERROR_LUA,
    GECND_ERROR_NO_GLY,
    GECND_ERROR_NO_GAME,
    GECND_ERROR_NO_ENGINE,
    GECND_ERROR_NO_LUA_VM,
    GECND_ERROR_NO_PATH_STR,
    GECND_ERROR_NO_FILE_STR,
    GECND_ERROR_MALLOC_FAIL
} gecnd_enum_error_code_t;

typedef struct {
    lua_State *L;
    gecnd_enum_control_mode_t control_mode;
    int ref_code_game;
    int ref_code_engine;
    int ref_native_callback_init;
    int ref_native_callback_loop;
    int ref_native_callback_draw;
    int ref_native_callback_keyboard;
} gecnd_t;

// instance
gecnd_t *gecnd_new();
void gecnd_destroy(gecnd_t *gly);
// configure
gecnd_enum_error_code_t gecnd_lua_open_graphics(gecnd_t *gly);
gecnd_enum_error_code_t gecnd_set_game_file(gecnd_t *gly, const char *const path, const char *const file);
gecnd_enum_error_code_t gecnd_set_engine_file(gecnd_t *gly, const char *const path, const char *const file);
gecnd_enum_error_code_t gecnd_set_control_mode(gecnd_t *gly, gecnd_enum_control_mode_t mode);
// callbacks
gecnd_enum_error_code_t gecnd_native_callback_init(gecnd_t *gly, int16_t width, int16_t height);
gecnd_enum_error_code_t gecnd_native_callback_loop(gecnd_t *gly, int16_t delta_time);
gecnd_enum_error_code_t gecnd_native_callback_draw(gecnd_t *gly);
gecnd_enum_error_code_t gecnd_native_callback_keyboard(gecnd_t *gly, char *const key, bool pressed);

const char *const gecnd_utils_get_cwd(void);
const char *const gecnd_utils_get_exe_cwd(void);
