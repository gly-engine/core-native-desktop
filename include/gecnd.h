#include <stdbool.h>
#include <stdint.h>

#define GLY_REGISTRYINDEX ((uint32_t)(uintptr_t)(gecnd_new))

#define GECND_FLAG_NONE   (0u)
#define GECND_FLAG_TIMER_FIXED          (0u)
#define GECND_FLAG_TIMER_INTERNAL       (1u)
#define GECND_FLAG_TIMER_BACKEND        (2u)
#define GECND_FLAG_TIMER_PREFER_BACKEND (3u)

#ifndef DOXYGEN
#define GECND_INTERNAL_MALLOC           (1u)
#define GECND_INTERNAL_RUNNING          (2u)
#define GECND_INTERNAL_WANT_EXIT        (4u)
#endif

// alias:
#define gecnd_add_flags(gly)  gecnd_set_flags(gly, gecnd_get_flags(gly) | FLAG_A)
#define gecnd_del_flags(gly)  gecnd_set_flags(gly, gecnd_get_flags(gly) & ~FLAG_A); 

//! @cond
typedef struct lua_State lua_State;
//! @endcond

typedef struct {
    lua_State *L;
    void* loop;
    uint8_t target_fps;
    uint8_t frameskip;
    uint8_t frameskip_count;
    uint8_t flags;
    uint8_t internal;
    int16_t width;
    int16_t height;
    int16_t delta_time;
    int ref_native_callback_init;
    int ref_native_callback_loop;
    int ref_native_callback_draw;
    int ref_native_callback_keyboard;
    char *lua_game_code;
    char *lua_engine_code;
    const char* error_string;
} gecnd_t;

// instance
gecnd_t *gecnd_new(lua_State* L);
void gecnd_destroy(gecnd_t *gly);
// configure
void gecnd_set_loop(gecnd_t *gly, void* loop);
void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]);
void gecnd_set_delta(gecnd_t *gly, int16_t ms);
void gecnd_set_flags(gecnd_t *gly, int32_t flags);
void gecnd_set_screensize(gecnd_t *gly, int16_t width, int16_t height);
// status
uint32_t gecnd_get_flags(gecnd_t *gly);
uint32_t gecnd_get_sleep(gecnd_t *gly);
// error
bool gecnd_has_errors(gecnd_t *gly);
const char* gecnd_get_errors(gecnd_t *gly);
// tick
bool gecnd_update(gecnd_t * gly);
// legacy:
void gecnd_set_game_file(gecnd_t *gly, const char *const path, const char *const file);
void gecnd_set_engine_file(gecnd_t *gly, const char *const path, const char *const file);
// utils
uint32_t gecnd_get_delta_ms(void);
char *gecnd_utils_get_cwd(void);
char *gecnd_utils_get_exe_cwd(void);
