#include "gemetrics.h"
#include "gehook.h"
#include <stdio.h>
#include <string.h>
#include <lua.h>

#if defined(LUA_LJDIR)
#define L_FLAVOR "JIT"
#else
#define L_FLAVOR "PUC"
#endif

#define FMT_FPS  "FPS: %d avg | %d cur | %d low (1%%) | %d drops"
#define FMT_LUA  "Lua %s (%s %db) | %s cur | %s avg | %s peak"
#define FMT_PERF "Perf: I/O %dms | Loop %dms | Draw %dms | Post %dms | Wait %dms"

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

    const int16_t box_x = 8;
    const int16_t box_y = 8;
    const int16_t box_w = 380;
    const int16_t line_h = 16;
    const int16_t padding = 8;
    const int16_t box_h = (3 * line_h) + padding;
    
    char buf[256];
    char s1[32], s2[32], s3[32];

    native_draw_color(0xFFFF00FF);
    native_draw_rect(0, box_x, box_y, box_w, box_h, 0);

    native_draw_color(0x000000FF);
    native_text_font_default(1);
    native_text_font_size(14);

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
             gecnd_metrics_get_fps_worst(), gecnd_metrics_get_fps_drops());
    native_text_print(tx, ty, buf);
    ty += line_h;


    snprintf(buf, sizeof(buf), FMT_PERF,
             gecnd_metrics_get_input_worst(), gecnd_metrics_get_loop_time(),
             gecnd_metrics_get_draw_time(), gecnd_metrics_get_post_time(),
             gecnd_metrics_get_wait_time());
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
           gecnd_metrics_get_fps_worst(), gecnd_metrics_get_fps_drops());
    printf("  " FMT_PERF "\n",
           gecnd_metrics_get_input_worst(), gecnd_metrics_get_loop_time(),
           gecnd_metrics_get_draw_time(), gecnd_metrics_get_post_time(),
           gecnd_metrics_get_wait_time());
}
