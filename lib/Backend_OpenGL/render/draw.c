#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gecnd.h"
#include "gebuffer.h"
#include "gefilter.h"
#include "gehook.h"
#include "geopengl.h"

void ge_batch_get_color_u8(uint8_t *c) {
    GLBackendState *s = geogl_get_state();
    memcpy(c, s->current_color.rgba, 4);
}

void native_draw_start(void) {
    ge_pipeline_start();
    native_draw_background_video();
}

void native_draw_flush() {
    //ge_pipeline_flush();
}

void native_draw_finish(void) {
    ge_pipeline_flush();
    platform_swap_buffers();
}

void native_draw_color(uint32_t color)
{
    geogl_get_state()->current_color.u32 = __builtin_bswap32(color);
}

void native_draw_clear(uint32_t color) {
    if (gecnd_get_background_frame()) return;
    GLBackendState *s = geogl_get_state();
    native_draw_color(color);
    native_draw_rect(0, 0, 0, s->window_width, s->window_height, 0);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *s = geogl_get_state();
    uint32_t color = s->current_color.u32;
    uint8_t alpha = (color >> 24) & 0xFF;
    bool opaque = (alpha >= 254);
    float wu = s->white_uv[0], wv = s->white_uv[1];

    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    if (mode == 0) {
        if (r == 0) {
            ge_batch_add_vertex_tex(x,     y,     wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,     y + h, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y + h, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,     y,     wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y + h, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y,     wu, wv, color, opaque, 0);
        } else {
            float u0 = s->corner_uv[0], v0 = s->corner_uv[1];
            float u1 = s->corner_uv[2], v1 = s->corner_uv[3];

            if (h > 2 * r) {
                ge_batch_add_vertex_tex(x,     y + r,     wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x,     y + h - r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w, y + h - r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x,     y + r,     wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w, y + h - r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w, y + r,     wu, wv, color, opaque, 0);
            }
            if (w > 2 * r) {
                ge_batch_add_vertex_tex(x + r,     y,     wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + r,     y + r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w - r, y + r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + r,     y,     wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w - r, y + r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w - r, y,     wu, wv, color, opaque, 0);

                ge_batch_add_vertex_tex(x + r,     y + h - r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + r,     y + h,     wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w - r, y + h,     wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + r,     y + h - r, wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w - r, y + h,     wu, wv, color, opaque, 0);
                ge_batch_add_vertex_tex(x + w - r, y + h - r, wu, wv, color, opaque, 0);
            }

            ge_batch_add_vertex_tex(x,     y,     u0, v0, color, false, 0);
            ge_batch_add_vertex_tex(x,     y + r, u0, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + r, y + r, u1, v1, color, false, 0);
            ge_batch_add_vertex_tex(x,     y,     u0, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + r, y + r, u1, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + r, y,     u1, v0, color, false, 0);

            ge_batch_add_vertex_tex(x + w - r, y,     u1, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + w - r, y + r, u1, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + w,     y + r, u0, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + w - r, y,     u1, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + w,     y + r, u0, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + w,     y,     u0, v0, color, false, 0);

            ge_batch_add_vertex_tex(x,     y + h - r, u0, v1, color, false, 0);
            ge_batch_add_vertex_tex(x,     y + h,     u0, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + r, y + h,     u1, v0, color, false, 0);
            ge_batch_add_vertex_tex(x,     y + h - r, u0, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + r, y + h,     u1, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + r, y + h - r, u1, v1, color, false, 0);

            ge_batch_add_vertex_tex(x + w - r, y + h - r, u1, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + w - r, y + h,     u1, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + w,     y + h,     u0, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + w - r, y + h - r, u1, v1, color, false, 0);
            ge_batch_add_vertex_tex(x + w,     y + h,     u0, v0, color, false, 0);
            ge_batch_add_vertex_tex(x + w,     y + h - r, u0, v1, color, false, 0);
        }
    } else {
        if (r == 0) {
            int16_t lw = 1;
            ge_batch_add_vertex_tex(x,     y,      wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,     y + lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y + lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,     y,      wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y + lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y,      wu, wv, color, opaque, 0);

            ge_batch_add_vertex_tex(x,     y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,     y + h,      wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y + h,      wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,     y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y + h,      wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w, y + h - lw, wu, wv, color, opaque, 0);

            ge_batch_add_vertex_tex(x,      y + lw,     wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,      y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + lw, y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x,      y + lw,     wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + lw, y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + lw, y + lw,     wu, wv, color, opaque, 0);

            ge_batch_add_vertex_tex(x + w - lw, y + lw,     wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w - lw, y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w,      y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w - lw, y + lw,     wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w,      y + h - lw, wu, wv, color, opaque, 0);
            ge_batch_add_vertex_tex(x + w,      y + lw,     wu, wv, color, opaque, 0);
        } else {
            float fw = (float)w, fh = (float)h, fr = (float)r;
            float hw = fw * 0.5f, hh = fh * 0.5f;
            float pad = 2.0f;
            float x1 = (float)x - pad,      y1 = (float)y - pad;
            float x2 = (float)x + fw + pad,  y2 = (float)y + fh + pad;
            float px1 = -hw - pad, py1 = -hh - pad;
            float px2 =  hw + pad, py2 =  hh + pad;

            ge_batch_add_vertex_complex(x1, y1, px1, py1, hw, hh, fr, color, opaque);
            ge_batch_add_vertex_complex(x1, y2, px1, py2, hw, hh, fr, color, opaque);
            ge_batch_add_vertex_complex(x2, y2, px2, py2, hw, hh, fr, color, opaque);
            ge_batch_add_vertex_complex(x1, y1, px1, py1, hw, hh, fr, color, opaque);
            ge_batch_add_vertex_complex(x2, y2, px2, py2, hw, hh, fr, color, opaque);
            ge_batch_add_vertex_complex(x2, y1, px2, py1, hw, hh, fr, color, opaque);
        }
    }
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    GLBackendState *s = geogl_get_state();
    uint32_t color = s->current_color.u32;
    uint8_t alpha = (color >> 24) & 0xFF;
    bool opaque = (alpha >= 254);
    float wu = s->white_uv[0], wv = s->white_uv[1];
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float lw = GE_LINE_WIDTH / 2.0f;
    float px[4] = {(float)x1 + nx * lw, (float)x2 + nx * lw, (float)x2 - nx * lw, (float)x1 - nx * lw};
    float py[4] = {(float)y1 + ny * lw, (float)y2 + ny * lw, (float)y2 - ny * lw, (float)y1 - ny * lw};

    ge_batch_add_vertex_tex((int16_t)px[0], (int16_t)py[0], wu, wv, color, opaque, 0);
    ge_batch_add_vertex_tex((int16_t)px[1], (int16_t)py[1], wu, wv, color, opaque, 0);
    ge_batch_add_vertex_tex((int16_t)px[2], (int16_t)py[2], wu, wv, color, opaque, 0);
    ge_batch_add_vertex_tex((int16_t)px[0], (int16_t)py[0], wu, wv, color, opaque, 0);
    ge_batch_add_vertex_tex((int16_t)px[2], (int16_t)py[2], wu, wv, color, opaque, 0);
    ge_batch_add_vertex_tex((int16_t)px[3], (int16_t)py[3], wu, wv, color, opaque, 0);
}
