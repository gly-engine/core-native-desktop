#include "gemetrics.h"
#include <string.h>
#include <lua.h>

typedef struct
{
    uint32_t flags;

    uint64_t input_start;
    uint32_t input_time;

    uint64_t loop_start;
    uint32_t loop_time;

    uint64_t draw_start;
    uint32_t draw_time;

    uint64_t post_start;
    uint32_t post_time;

    uint64_t tint_start;
    uint32_t tint_time;

    uint64_t wait_start;
    uint32_t wait_time;

    uint32_t frame_count;
    uint64_t last_sec_time;

    uint16_t fps_average;
    uint16_t fps_immediate;
    uint16_t fps_worst;
    uint16_t fps_percent;
    uint16_t fps_drops;

    uint32_t current_sec_good_frames;
    uint16_t current_sec_worst_fps;
    uint32_t current_sec_drops;
    uint64_t last_frame_time;

    uint32_t input_worst_cur_sec;
    uint32_t input_worst_last_sec;

    uint64_t lua_mem_peak;
    uint64_t lua_mem_avg;
    uint64_t lua_mem_accumulated;
    uint32_t lua_mem_frame_count;
} metrics_state_t;

static metrics_state_t state = {0};

void gecnd_profile_ip_tick();

void gecnd_metrics_setup(uint32_t flags)
{
    state.flags = flags;
}

uint32_t gecnd_metrics_get_flags(void)
{
    return state.flags;
}

void gecnd_metrics_start_input(void)
{
    if (state.flags)
    {
        state.input_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_input(void)
{
    if (state.flags)
    {
        state.input_time = (uint32_t)(gecnd_get_cur_time() - state.input_start);
        if (state.input_time > state.input_worst_cur_sec)
        {
            state.input_worst_cur_sec = state.input_time;
        }
    }
}

void gecnd_metrics_start_loop(void)
{
    if (state.flags)
    {
        state.loop_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_loop(void)
{
    if (state.flags)
    {
        state.loop_time = (uint32_t)(gecnd_get_cur_time() - state.loop_start);
    }
}

void gecnd_metrics_start_draw(void)
{
    if (state.flags)
    {
        state.draw_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_draw(void)
{
    if (state.flags)
    {
        state.draw_time = (uint32_t)(gecnd_get_cur_time() - state.draw_start);
    }
}

void gecnd_metrics_start_post(void)
{
    if (state.flags)
    {
        state.post_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_post(void)
{
    if (state.flags)
    {
        state.post_time = (uint32_t)(gecnd_get_cur_time() - state.post_start);
    }
}

void gecnd_metrics_start_tint(void)
{
    if (state.flags)
    {
        state.tint_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_tint(void)
{
    if (state.flags)
    {
        state.tint_time = (uint32_t)(gecnd_get_cur_time() - state.tint_start);
    }
}

void gecnd_metrics_start_wait(void)
{
    if (state.flags)
    {
        state.wait_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_wait(void)
{
    if (state.flags)
    {
        state.wait_time = (uint32_t)(gecnd_get_cur_time() - state.wait_start);
    }
}

void gecnd_metrics_update(void)
{
    if (!state.flags) return;
    gecnd_metrics_print();
    gecnd_profile_ip_tick();

    uint64_t now = gecnd_get_cur_time();
    state.frame_count++;

    if (state.last_frame_time == 0)
    {
        state.last_frame_time = now;
        state.last_sec_time = now;
        return;
    }

    uint32_t frame_ms = (uint32_t)(now - state.last_frame_time);
    state.last_frame_time = now;

    if (frame_ms > 0)
    {
        state.fps_immediate = 1000 / frame_ms;
    }
    else
    {
        state.fps_immediate = 999; // cap for 0ms frames
    }

    gecnd_t *root = gecnd_get_root();
    uint8_t target = (root && root->target_fps > 0) ? root->target_fps : 60;

    // Tolerance: 59 for 60 target
    if (state.fps_immediate >= (target - 1))
    {
        state.current_sec_good_frames++;
    }

    uint16_t ref = state.fps_average > 0 ? state.fps_average : target;
    if (state.fps_immediate < (ref - 2))
    {
        state.current_sec_drops++;
    }

    if (state.fps_immediate < state.current_sec_worst_fps || state.current_sec_worst_fps == 0)
    {
        state.current_sec_worst_fps = state.fps_immediate;
    }

    if (root && root->L)
    {
        uint64_t count = (uint64_t)lua_gc(root->L, LUA_GCCOUNT, 0);
        uint64_t countb = (uint64_t)lua_gc(root->L, LUA_GCCOUNTB, 0);
        uint64_t mem = (count * 1024) + countb;
        
        if (mem > state.lua_mem_peak)
        {
            state.lua_mem_peak = mem;
        }

        state.lua_mem_accumulated += mem;
        state.lua_mem_frame_count++;
    }

    if (now - state.last_sec_time >= 1000)
    {
        state.fps_average = state.frame_count;
        state.fps_worst = state.current_sec_worst_fps;
        state.fps_drops = (uint16_t)state.current_sec_drops;
        state.input_worst_last_sec = state.input_worst_cur_sec;
        
        float stability = (float)state.current_sec_good_frames / (state.frame_count > 0 ? state.frame_count : 1);
        float speed = (float)state.fps_average / target;
        
        if (speed > 1.05f) 
        {
            state.fps_percent = (uint16_t)(speed * 100);
        }
        else
        {
            state.fps_percent = (uint16_t)(stability * 100);
        }

        if (state.lua_mem_frame_count > 0)
        {
            state.lua_mem_avg = state.lua_mem_accumulated / state.lua_mem_frame_count;
        }
        
        state.frame_count = 0;
        state.last_sec_time = now;
        state.current_sec_worst_fps = 0;
        state.current_sec_good_frames = 0;
        state.current_sec_drops = 0;
        state.input_worst_cur_sec = 0;
        state.lua_mem_accumulated = 0;
        state.lua_mem_frame_count = 0;
    }
}

/* The core only fires registry events (core:pre_loop, core:pre_draw, ...);
 * metrics is not wired in update.c. On the first core:pre_loop, if profiling
 * is enabled, the remaining events are hooked and collection starts. The
 * pre_* hooks land before the backend ones (hooks are prepended), so
 * core:pre_tint measures the backend flush and post_draw renders the overlay
 * before it. */
static void on_post_loop(const char *key, void *value, void *usr) {
    (void)key; (void)value; (void)usr;
    gecnd_metrics_finish_loop();
}

static void on_pre_draw(const char *key, void *value, void *usr) {
    (void)key; (void)value; (void)usr;
    gecnd_metrics_start_draw();
}

static void on_post_draw(const char *key, void *value, void *usr) {
    (void)key; (void)usr;
    gecnd_metrics_finish_draw();
    gecnd_metrics_render((gecnd_t *)value);
}

static void on_pre_tint(const char *key, void *value, void *usr) {
    (void)key; (void)value; (void)usr;
    gecnd_metrics_start_tint();
}

static void on_post_tint(const char *key, void *value, void *usr) {
    (void)key; (void)value; (void)usr;
    gecnd_metrics_finish_tint();
    gecnd_metrics_start_wait();
}

static void on_pre_loop(const char *key, void *value, void *usr) {
    (void)key; (void)value; (void)usr;
    static bool registered = false;
    if (!registered && state.flags) {
        registered = true;
        gecnd_registry("hook", "core:post_loop", (void *)on_post_loop, NULL);
        gecnd_registry("hook", "core:pre_draw",  (void *)on_pre_draw,  NULL);
        gecnd_registry("hook", "core:post_draw", (void *)on_post_draw, NULL);
        gecnd_registry("hook", "core:pre_tint",  (void *)on_pre_tint,  NULL);
        gecnd_registry("hook", "core:post_tint", (void *)on_post_tint, NULL);
    }
    gecnd_metrics_finish_wait();
    gecnd_metrics_update();
    gecnd_metrics_start_loop();
}

/* @todo temporário: liga/desliga metrics em runtime pelo Lua
 * (0 = off, 1 = print, 2 = draw, 3 = ambos) */
static char *lua_native_debug_set(uint8_t flags) {
    gecnd_metrics_setup(flags);
    return NULL;
}

__attribute__((constructor))
static void init(void) {
    gecnd_registry("hook", "core:pre_loop", (void *)on_pre_loop, NULL);
    gecnd_registry("set", "lua_global_ffi:native_debug_set+$u8+$0", (void *)lua_native_debug_set, NULL);
}

uint64_t gecnd_metrics_get_lua_peak(void) { return state.lua_mem_peak; }
uint64_t gecnd_metrics_get_lua_avg(void) { return state.lua_mem_avg; }
uint32_t gecnd_metrics_get_input_time(void) { return state.input_time; }
uint32_t gecnd_metrics_get_input_worst(void) { return state.input_worst_last_sec; }
uint32_t gecnd_metrics_get_loop_time(void) { return state.loop_time; }
uint32_t gecnd_metrics_get_draw_time(void) { return state.draw_time; }
uint32_t gecnd_metrics_get_post_time(void) { return state.post_time; }
uint32_t gecnd_metrics_get_tint_time(void) { return state.tint_time; }
uint32_t gecnd_metrics_get_wait_time(void) { return state.wait_time; }
uint16_t gecnd_metrics_get_fps_avg(void) { return state.fps_average; }
uint16_t gecnd_metrics_get_fps_immediate(void) { return state.fps_immediate; }
uint16_t gecnd_metrics_get_fps_worst(void) { return state.fps_worst; }
uint16_t gecnd_metrics_get_fps_percent(void) { return state.fps_percent; }
uint16_t gecnd_metrics_get_fps_drops(void) { return state.fps_drops; }
