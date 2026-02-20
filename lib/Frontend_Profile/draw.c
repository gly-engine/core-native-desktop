#include "gemetrics.h"
#include "gehook.h"
#include <stdio.h>
#include <lua.h>

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
    if (flags & GECND_METRICS_FPS) r_lines += 1;
    if (flags & GECND_METRICS_LUA) r_lines += 2;

    int16_t l_lines = 0;
    if (flags & GECND_METRICS_PERF) l_lines += 5;

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
            snprintf(buf, sizeof(buf), "FPS: %d avg: %d worst: %d", 
                     gecnd_metrics_get_fps_immediate(),
                     gecnd_metrics_get_fps_avg(),
                     gecnd_metrics_get_fps_worst());
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

            snprintf(buf, sizeof(buf), "Lua memory cur: %s", s1);
            native_text_print(rx, ry, buf);
            ry += line_h;

            snprintf(buf, sizeof(buf), "avg: %s peak: %s", s2, s3);
            native_text_print(rx, ry, buf);
            ry += line_h;
        }
    }

    if (l_lines > 0)
    {
        int16_t ly = margin + (margin / 2);
        uint32_t io = gecnd_metrics_get_input_time();
        uint32_t loop = gecnd_metrics_get_loop_time();
        uint32_t draw = gecnd_metrics_get_draw_time();
        uint32_t post = gecnd_metrics_get_post_time();
        uint32_t tint = gecnd_metrics_get_tint_time();
        uint32_t total = io + loop + draw + post + tint;

        if (total > 0)
        {
            snprintf(buf, sizeof(buf), "IO: %d ms (%.1f%%)", io, (io * 100.0f) / total);
            native_text_print(margin + (margin / 2), ly, buf);
            ly += line_h;

            snprintf(buf, sizeof(buf), "Loop: %d ms (%.1f%%)", loop, (loop * 100.0f) / total);
            native_text_print(margin + (margin / 2), ly, buf);
            ly += line_h;

            snprintf(buf, sizeof(buf), "Draw: %d ms (%.1f%%)", draw, (draw * 100.0f) / total);
            native_text_print(margin + (margin / 2), ly, buf);
            ly += line_h;

            snprintf(buf, sizeof(buf), "Post: %d ms (%.1f%%)", post, (post * 100.0f) / total);
            native_text_print(margin + (margin / 2), ly, buf);
            ly += line_h;

            snprintf(buf, sizeof(buf), "Tint: %d ms (%.1f%%)", tint, (tint * 100.0f) / total);
            native_text_print(margin + (margin / 2), ly, buf);
        }
    }
}
