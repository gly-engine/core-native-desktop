#include "gemetrics.h"
#include <stdio.h>
#include <lua.h>

static void format_memory(uint64_t bytes, char *out, size_t size)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
    {
        snprintf(out, size, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
    else if (bytes >= 1024ULL * 1024ULL)
    {
        snprintf(out, size, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024ULL)
    {
        snprintf(out, size, "%.2f KB", (double)bytes / 1024.0);
    }
    else
    {
        snprintf(out, size, "%llu B", (unsigned long long)bytes);
    }
}

void gecnd_metrics_print(void)
{
    uint32_t flags = gecnd_metrics_get_flags();

    gecnd_t *root = gecnd_get_root();
    if (!root) return;

    char s_cur[32], s_avg[32], s_peak[32];
    
    uint64_t cur_mem = 0;
    if (root->L) {
        uint64_t count = (uint64_t)lua_gc(root->L, LUA_GCCOUNT, 0);
        uint64_t countb = (uint64_t)lua_gc(root->L, LUA_GCCOUNTB, 0);
        cur_mem = (count * 1024) + countb;
    }

    uint16_t fps = gecnd_metrics_get_fps_avg();
    format_memory(cur_mem, s_cur, sizeof(s_cur));
    format_memory(gecnd_metrics_get_lua_avg(), s_avg, sizeof(s_avg));
    format_memory(gecnd_metrics_get_lua_peak(), s_peak, sizeof(s_peak));

    printf("[METRICS] FPS avg: %u | z-depth: %.3f | Lua Mem: cur=%s, avg=%s, peak=%s\n",
           fps, 1.0, s_cur, s_avg, s_peak);
}
