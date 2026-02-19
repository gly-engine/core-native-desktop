#ifndef GECND_H
#define GECND_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

typedef struct {
    float x, y;
} gecnd_vec2;

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

typedef struct {
    gecnd_vec2 corners[4];
    gecnd_vec2 video_pos;
    gecnd_vec2 video_size;
    float brightness;
    float contrast;
    float saturation;
    float film_grain;
    float aa_blur;
    float aa_weight_center;
    float aa_weight_neighbor;
    float rotation;
    float crt_amount;
    float scratch_amount;
    float jitter_amount;
} gecnd_filter_t;

// instance
gecnd_t *gecnd_new(lua_State* L);
gecnd_t *gecnd_get_root();
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
void gecnd_set_btn_state(gecnd_t *gly, const char* key, bool state);
// utils
uint32_t gecnd_get_delta_ms(void);
size_t gecnd_utils_get_exe_cwd(char *buffer, size_t max_size);
size_t gecnd_utils_get_cwd(char *buffer, size_t max_size);
// filters
gecnd_filter_t* gecnd_filter_get_config();
void gecnd_filter_set_brightness(float v);
void gecnd_filter_set_contrast(float v);
void gecnd_filter_set_saturation(float v);
void gecnd_filter_set_film_grain(float v);
void gecnd_filter_set_crt(float v);
void gecnd_filter_set_scratch(float v);
void gecnd_filter_set_jitter(float v);
void gecnd_filter_set_video_pos(float x, float y, float w, float h);
void gecnd_filter_set_rotation(float angle);
void gecnd_filter_set_aa(float blur, float wC, float wN);
void gecnd_filter_set_corners(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4);
void gecnd_filter_reset_effects();
void gecnd_filter_reset_corners();
void gecnd_filter_reset_video_pos();
bool gencd_filter_is_zero_corners();
bool gencd_filter_is_zero_video_pos();

#endif
