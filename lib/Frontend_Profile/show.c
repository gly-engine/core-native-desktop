#include "gemetrics.h"
#include "gehook.h"
#include "genative.h"
#include <stdio.h>
#include <string.h>
#include <lua.h>

#if defined(LUAU_FASTMATH_BEGIN)
#define L_FLAVOR "LuaU"
#define LUA_RELEASE "Roblox"
#elif defined(LUA_LJDIR)
#define L_FLAVOR "JIT"
#else
#define L_FLAVOR "PUC"
#endif

#if !defined(LUA_RELEASE)
#define LUA_RELEASE "Lua 5.1"
#endif

#define FMT_FPS  "FPS:  %3d avg | %3d cur | %3d low (1%%) | %3d drops | " FMT_TEMP
#define FMT_LUA  "Lua %s (%s %db) | %9s cur | %9s avg | %9s peak"
#define FMT_PERF "Perf: I/O %4dms | Loop %4dms | Draw %4dms | Post %4dms | Wait %4dms"
#define FMT_TEMP "Temp: %3d\xc2\xb0""C"
#define FMT_SYS  "Sys:  %3d%% | free: %u MB | use: %u MB | total: %u MB"
#define FMT_CORE "Core: %3d%% | use: %s | peak: %s | reserved: %s"

int gecnd_profile_get_ip_count(void);
const char *gecnd_profile_get_ip_at(int i);
int gecnd_profile_get_mem(uint32_t *free_mb, uint32_t *used_mb, uint32_t *total_mb);
int gecnd_profile_get_core_mem(uint64_t *used, uint64_t *reserved, uint64_t *peak);

static void format_ips(char *out, size_t sz)
{
    int n = gecnd_profile_get_ip_count();
    if (n > 4) n = 4;
    size_t pos = 0;
    for (int i = 0; i < n && pos < sz - 2; i++)
        pos += (size_t)snprintf(out + pos, sz - pos,
            "%s%s", i ? " " : "", gecnd_profile_get_ip_at(i));
    if (pos == 0) snprintf(out, sz, "0.0.0.0");
}

static int percent_of_sys(uint32_t total_mb, uint64_t bytes)
{
    if (total_mb == 0) return -1;
    uint64_t total_bytes = (uint64_t)total_mb * 1024ULL * 1024ULL;
    return (int)((bytes * 100) / total_bytes);
}

static void format_memory(uint64_t bytes, char *out, size_t size)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        snprintf(out, size, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
    else if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, size, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024ULL) {
        snprintf(out, size, "%.1f KB", (double)bytes / 1024.0);
    }
    else {
        snprintf(out, size, "%llu B", (unsigned long long)bytes);
    }
}

void gecnd_metrics_render(gecnd_t *gly)
{
    uint32_t flags = gecnd_metrics_get_flags();
    if (!(flags & GECND_METRICS_DRAW) || !gly) return;

    uint32_t mem_free, mem_used, mem_total;
    int mem_pct = gecnd_profile_get_mem(&mem_free, &mem_used, &mem_total);

    uint64_t core_used, core_reserved, core_peak;
    int core_ok = gecnd_profile_get_core_mem(&core_used, &core_reserved, &core_peak);

    const int16_t box_x = 40;
    const int16_t box_y = 32;
    const int16_t box_w = 380;
    const int16_t line_h = 16;
    const int16_t padding = 8;
    const int16_t box_h = (int16_t)(((4 + (mem_pct >= 0 ? 1 : 0) + (core_ok == 0 ? 1 : 0)) * line_h) + padding);

    char buf[256];
    char s1[32], s2[32], s3[32];

    native_draw_color(0xFFFF00FF);
    native_draw_rect(0, box_x, box_y, box_w, box_h, 0);

    native_draw_color(0x000000FF);
    native_text_font_default(1);
    native_text_font_size(11);

    int16_t tx = box_x + (padding / 2);
    int16_t ty = box_y + (padding / 2);

    if (gly->L) {
        uint64_t count = (uint64_t)lua_gc(gly->L, LUA_GCCOUNT, 0);
        uint64_t countb = (uint64_t)lua_gc(gly->L, LUA_GCCOUNTB, 0);
        format_memory((count * 1024) + countb, s1, sizeof(s1));
        format_memory(gecnd_metrics_get_lua_avg(), s2, sizeof(s2));
        format_memory(gecnd_metrics_get_lua_peak(), s3, sizeof(s3));
        
        snprintf(buf, sizeof(buf), FMT_LUA, LUA_RELEASE, L_FLAVOR, (int)(sizeof(lua_Number) * 8), s1, s2, s3);
        native_text_print(tx, ty, buf);
        ty += line_h;
    }

    snprintf(buf, sizeof(buf), FMT_FPS,
             gecnd_metrics_get_fps_avg(), gecnd_metrics_get_fps_immediate(),
             gecnd_metrics_get_fps_worst(), gecnd_metrics_get_fps_drops(),
             gecnd_profile_get_temp());
    native_text_print(tx, ty, buf);
    ty += line_h;

    snprintf(buf, sizeof(buf), FMT_PERF,
             gecnd_metrics_get_input_worst(), gecnd_metrics_get_loop_time(),
             gecnd_metrics_get_draw_time(), gecnd_metrics_get_post_time(),
             gecnd_metrics_get_wait_time());
    native_text_print(tx, ty, buf);
    ty += line_h;

    if (mem_pct >= 0) {
        snprintf(buf, sizeof(buf), FMT_SYS, mem_pct, mem_free, mem_used, mem_total);
        native_text_print(tx, ty, buf);
        ty += line_h;
    }

    if (core_ok == 0) {
        format_memory(core_used, s1, sizeof(s1));
        format_memory(core_peak, s2, sizeof(s2));
        format_memory(core_reserved, s3, sizeof(s3));
        int peak_pct = percent_of_sys(mem_total, core_peak);
        snprintf(buf, sizeof(buf), FMT_CORE, peak_pct < 0 ? 0 : peak_pct, s1, s2, s3);
        native_text_print(tx, ty, buf);
        ty += line_h;
    }

    {
        char ips[128];
        format_ips(ips, sizeof(ips));
        snprintf(buf, sizeof(buf), "IP: %s", ips);
    }
    native_text_print(tx, ty, buf);
}

void gecnd_metrics_print(void)
{
    uint32_t flags = gecnd_metrics_get_flags();
    if (!(flags & GECND_METRICS_PRINT)) return;

    static uint64_t last_print = 0;
    uint64_t now = gecnd_get_cur_time();
    if (now - last_print < 1000) return;
    last_print = now;

    gecnd_t *gly = gecnd_get_root();
    if (!gly) return;

    char s1[32], s2[32], s3[32];
    if (gly->L) {
        uint64_t count = (uint64_t)lua_gc(gly->L, LUA_GCCOUNT, 0);
        uint64_t countb = (uint64_t)lua_gc(gly->L, LUA_GCCOUNTB, 0);
        format_memory((count * 1024) + countb, s1, sizeof(s1));
        format_memory(gecnd_metrics_get_lua_avg(), s2, sizeof(s2));
        format_memory(gecnd_metrics_get_lua_peak(), s3, sizeof(s3));
    } else {
        strcpy(s1, "0"); strcpy(s2, "0"); strcpy(s3, "0");
    }

    printf("[METRICS]\n");
    if (gly->L) {
        printf("  " FMT_LUA "\n", LUA_RELEASE, L_FLAVOR, (int)(sizeof(lua_Number) * 8), s1, s2, s3);
    }
    printf("  " FMT_FPS "\n",
           gecnd_metrics_get_fps_avg(), gecnd_metrics_get_fps_immediate(),
           gecnd_metrics_get_fps_worst(), gecnd_metrics_get_fps_drops(),
           gecnd_profile_get_temp());
    printf("  " FMT_PERF "\n",
           gecnd_metrics_get_input_worst(), gecnd_metrics_get_loop_time(),
           gecnd_metrics_get_draw_time(), gecnd_metrics_get_post_time(),
           gecnd_metrics_get_wait_time());
    uint64_t core_used = 0, core_reserved = 0, core_peak = 0;
    int core_ok = gecnd_profile_get_core_mem(&core_used, &core_reserved, &core_peak);

    uint32_t mem_free, mem_used, mem_total;
    int mem_pct = gecnd_profile_get_mem(&mem_free, &mem_used, &mem_total);
    if (mem_pct >= 0) {
        printf("  " FMT_SYS "\n", mem_pct, mem_free, mem_used, mem_total);
    }
    if (core_ok == 0) {
        format_memory(core_used, s1, sizeof(s1));
        format_memory(core_peak, s2, sizeof(s2));
        format_memory(core_reserved, s3, sizeof(s3));
        int peak_pct = percent_of_sys(mem_total, core_peak);
        printf("  " FMT_CORE "\n", peak_pct < 0 ? 0 : peak_pct, s1, s2, s3);
    }
    {
        char ips[128];
        format_ips(ips, sizeof(ips));
        printf("  IP: %s\n", ips);
    }
}
