#include "gemetrics.h"
#include "gehook.h"
#include <stdio.h>
#include <lua.h>

#if defined(LUA_LJDIR)
#define L_FLAVOR "JIT"
#else
#define L_FLAVOR "PUC"
#endif

static void format_memory(uint64_t bytes, char *out, size_t size)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
    {
        snprintf(out, size, "%llu GB", (unsigned long long)(bytes / (1024ULL * 1024ULL * 1024ULL)));
    }
    else if (bytes >= 1024ULL * 1024ULL)
    {
        snprintf(out, size, "%llu MB", (unsigned long long)(bytes / (1024ULL * 1024ULL)));
    }
    else if (bytes >= 1024ULL)
    {
        snprintf(out, size, "%llu KB", (unsigned long long)(bytes / 1024ULL));
    }
    else
    {
        snprintf(out, size, "%llu B", (unsigned long long)bytes);
    }
}

void gecnd_metrics_render(gecnd_t *gly)
{
    if (!gly)
    {
        return;
    }

    uint32_t flags = gecnd_metrics_get_flags();
    if (flags == 0)
    {
        return;
    }

    const int16_t margin = 16;
    const int16_t box_w = 160;
    const int16_t line_h = 20;
    bool has_bg = (flags & GECND_METRICS_BG);

    int16_t r_lines = 0;
    if (flags & GECND_METRICS_FPS) r_lines += 2;
    if (flags & GECND_METRICS_LUA) r_lines += 3;

    int16_t l_lines = 0;
    if (flags & GECND_METRICS_PERF) l_lines += 6;

    if (has_bg)
    {
        native_draw_color(0xFFFF00FF); // Amarelo
        if (r_lines > 0)
        {
            native_draw_rect(0, gly->width - box_w - margin, margin, box_w, (r_lines * line_h) + margin, 0);
        }
        if (l_lines > 0)
        {
            native_draw_rect(0, margin, margin, box_w, (l_lines * line_h) + margin, 0);
        }
    }

    native_draw_color(has_bg ? 0x000000FF : 0xFFFFFFFF);
    native_text_font_default(1);
    native_text_font_size(16);

    char buf[128];

    if (r_lines > 0)
    {
        int16_t rx = gly->width - box_w - (margin / 2);
        int16_t ry = margin + (margin / 2);

        if (flags & GECND_METRICS_FPS)
        {
            int fps_cur = gecnd_metrics_get_fps_immediate();
            int fps_avg = gecnd_metrics_get_fps_avg();
            int fps_worst = gecnd_metrics_get_fps_worst();
            int fps_drops = gecnd_metrics_get_fps_drops();

            snprintf(buf, sizeof(buf), "FPS: %3d (cur: %3d)", fps_avg, fps_cur);
            native_text_print(rx, ry, buf);
            ry += line_h;

            snprintf(buf, sizeof(buf), "Worst: %3d (drops: %d)", fps_worst, fps_drops);
            native_text_print(rx, ry, buf);
            ry += line_h;
        }

        if (flags & GECND_METRICS_LUA)
        {
            uint64_t count = (uint64_t)lua_gc(gly->L, LUA_GCCOUNT, 0);
            uint64_t countb = (uint64_t)lua_gc(gly->L, LUA_GCCOUNTB, 0);
            uint64_t mem = (count * 1024) + countb;
            char s1[32], s2[32], s3[32];
            format_memory(mem, s1, sizeof(s1));
            format_memory(gecnd_metrics_get_lua_avg(), s2, sizeof(s2));
            format_memory(gecnd_metrics_get_lua_peak(), s3, sizeof(s3));

            static const char *const nt = (lua_Number)0.5 == 0? "int": "fp";
            snprintf(buf, sizeof(buf), "%s %s %dbits %s", LUA_RELEASE, L_FLAVOR, (int)(sizeof(lua_Number) * 8), nt);
            native_text_print(rx, ry, buf);
            ry += line_h;

            snprintf(buf, sizeof(buf), "memory currenty: %02s", s1);
            native_text_print(rx, ry, buf);
            ry += line_h;

            snprintf(buf, sizeof(buf), "avg: %02s peak: %02s", s2, s3);
            native_text_print(rx, ry, buf);
            ry += line_h;
        }
    }

    if (l_lines > 0)
    {
        int16_t ly = margin + (margin / 2);
        uint32_t io_worst = gecnd_metrics_get_input_worst();
        uint32_t loop = gecnd_metrics_get_loop_time();
        uint32_t draw = gecnd_metrics_get_draw_time();
        uint32_t post = gecnd_metrics_get_post_time();
        uint32_t tint = gecnd_metrics_get_tint_time();
        uint32_t wait = gecnd_metrics_get_wait_time();
        uint32_t total = io_worst + loop + draw + post + tint + wait;
        if (total == 0) total = 1;

        snprintf(buf, sizeof(buf), "I/O: %3d ms (%.1f%%)", io_worst, (io_worst * 100.0f) / total);
        native_text_print(margin + (margin / 2), ly, buf);
        ly += line_h;

        snprintf(buf, sizeof(buf), "Loop: %3d ms (%.1f%%)", loop, (loop * 100.0f) / total);
        native_text_print(margin + (margin / 2), ly, buf);
        ly += line_h;

        snprintf(buf, sizeof(buf), "Draw: %3d ms (%.1f%%)", draw, (draw * 100.0f) / total);
        native_text_print(margin + (margin / 2), ly, buf);
        ly += line_h;

        snprintf(buf, sizeof(buf), "Post: %3d ms (%.1f%%)", post, (post * 100.0f) / total);
        native_text_print(margin + (margin / 2), ly, buf);
        ly += line_h;

        snprintf(buf, sizeof(buf), "Tint: %3d ms (%.1f%%)", tint, (tint * 100.0f) / total);
        native_text_print(margin + (margin / 2), ly, buf);
        ly += line_h;

        snprintf(buf, sizeof(buf), "Wait: %3d ms (%.1f%%)", wait, (wait * 100.0f) / total);
        native_text_print(margin + (margin / 2), ly, buf);
    }
}
