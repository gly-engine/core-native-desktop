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

    uint32_t frame_count;
    uint64_t last_sec_time;

    uint16_t fps_average;
    uint16_t fps_immediate;
    uint16_t fps_worst;
    uint16_t fps_percent;

    uint32_t current_sec_good_frames;
    uint16_t current_sec_worst_fps;
    uint64_t last_frame_time;

    uint64_t lua_mem_peak;
    uint64_t lua_mem_avg;
    uint64_t lua_mem_accumulated;
    uint32_t lua_mem_frame_count;
} metrics_state_t;

static metrics_state_t state = {0};

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
    if (state.flags & GECND_METRICS_PERF)
    {
        state.input_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_input(void)
{
    if (state.flags & GECND_METRICS_PERF)
    {
        state.input_time = (uint32_t)(gecnd_get_cur_time() - state.input_start);
    }
}

void gecnd_metrics_start_loop(void)
{
    if (state.flags & GECND_METRICS_PERF)
    {
        state.loop_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_loop(void)
{
    if (state.flags & GECND_METRICS_PERF)
    {
        state.loop_time = (uint32_t)(gecnd_get_cur_time() - state.loop_start);
    }
}

void gecnd_metrics_start_draw(void)
{
    if (state.flags & GECND_METRICS_PERF)
    {
        state.draw_start = gecnd_get_cur_time();
    }
}

void gecnd_metrics_finish_draw(void)
{
    if (state.flags & GECND_METRICS_PERF)
    {
        state.draw_time = (uint32_t)(gecnd_get_cur_time() - state.draw_start);
    }
}

void gecnd_metrics_update(void)
{
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

    if (state.fps_immediate < state.current_sec_worst_fps || state.current_sec_worst_fps == 0)
    {
        state.current_sec_worst_fps = state.fps_immediate;
    }

    if (state.flags & GECND_METRICS_LUA)
    {
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
    }

    if (now - state.last_sec_time >= 1000)
    {
        state.fps_average = state.frame_count;
        state.fps_worst = state.current_sec_worst_fps;
        
        // Calculate percentage: (current_avg / target) * 100
        // But user asked for 100% if frames are stable.
        // Let's use (good_frames / total_frames) * 100 for stability,
        // but also consider the speed (avg_fps / target_fps).
        
        float stability = (float)state.current_sec_good_frames / state.frame_count;
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
        state.lua_mem_accumulated = 0;
        state.lua_mem_frame_count = 0;
    }
}

uint64_t gecnd_metrics_get_lua_peak(void) { return state.lua_mem_peak; }
uint64_t gecnd_metrics_get_lua_avg(void) { return state.lua_mem_avg; }
uint32_t gecnd_metrics_get_input_time(void) { return state.input_time; }
uint32_t gecnd_metrics_get_loop_time(void) { return state.loop_time; }
uint32_t gecnd_metrics_get_draw_time(void) { return state.draw_time; }
uint16_t gecnd_metrics_get_fps_avg(void) { return state.fps_average; }
uint16_t gecnd_metrics_get_fps_immediate(void) { return state.fps_immediate; }
uint16_t gecnd_metrics_get_fps_worst(void) { return state.fps_worst; }
uint16_t gecnd_metrics_get_fps_percent(void) { return state.fps_percent; }
