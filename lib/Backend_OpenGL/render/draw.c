#include <stdio.h>
#include <string.h>
#include <math.h>

#include "gecnd.h"
#include "gdmsp.h"
#include "gefilter.h"
#include "gehook.h"
#include "geopengl.h"

static bool enabled_gl = false;

static void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r);

static void core_pre_draw(const char *key, void *value, void *usr) {
    if (!enabled_gl) return;
    (void)key; (void)usr;
    gecnd_t *gly = (gecnd_t *)value;
    if (gly->state != GECND_FSM_RUNNING_PERFORMANCE &&
        gly->state != GECND_FSM_RUNNING_BACKGROUND) {
        glFinish();
    }
    ge_pipeline_start();
    native_draw_background_video();
}

static void core_pre_tint(const char *key, void *value, void *usr) {
    if (!enabled_gl) return;
    (void)key; (void)value; (void)usr;
    ge_pipeline_flush();
    platform_swap_buffers();
}

static void native_draw_color(uint32_t color)
{
    geogl_get_state()->current_color.u32 = __builtin_bswap32(color);
}

static void native_draw_clear(uint32_t color) {
    if (gdmsp_control()->is_active()) return;
    GLBackendState *s = geogl_get_state();
    native_draw_color(color);
    native_draw_rect(0, 0, 0, s->window_width, s->window_height, 0);
}

static void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    GLBackendState *s = geogl_get_state();
    uint32_t color = s->current_color.u32;

    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    if (mode == 0) {
        if (r == 0) {
            /* square fill */
            ge_batch_add_vertex_shape(x, y, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y + h, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + h, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + h, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y, 0, 0, 0, color, 0, false);
        } else {
            float u0 = s->corner_uv[0], v0 = s->corner_uv[1];
            float u1 = s->corner_uv[2], v1 = s->corner_uv[3];
            int cp = s->corner_page_index;

            /* square fill - center vertical strip (shape, no tex sample needed) */
            if (h > 2 * r) {
                ge_batch_add_vertex_shape(x,     y + r,     0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x,     y + h - r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w, y + h - r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x,     y + r,     0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w, y + h - r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w, y + r,     0, 0, 0, color, 0, false);
            }
            if (w > 2 * r) {
                /* edge top */
                ge_batch_add_vertex_shape(x + r,     y,     0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + r,     y + r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w - r, y + r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + r,     y,     0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w - r, y + r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w - r, y,     0, 0, 0, color, 0, false);

                /* edge bottom */
                ge_batch_add_vertex_shape(x + r,     y + h - r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + r,     y + h,     0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w - r, y + h,     0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + r,     y + h - r, 0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w - r, y + h,     0, 0, 0, color, 0, false);
                ge_batch_add_vertex_shape(x + w - r, y + h - r, 0, 0, 0, color, 0, false);
            }

            /* corner TL */
            ge_batch_add_vertex_alpha(x,     y,     u0, v0, color, cp);
            ge_batch_add_vertex_alpha(x,     y + r, u0, v1, color, cp);
            ge_batch_add_vertex_alpha(x + r, y + r, u1, v1, color, cp);
            ge_batch_add_vertex_alpha(x,     y,     u0, v0, color, cp);
            ge_batch_add_vertex_alpha(x + r, y + r, u1, v1, color, cp);
            ge_batch_add_vertex_alpha(x + r, y,     u1, v0, color, cp);

            /* corner TR */
            ge_batch_add_vertex_alpha(x + w - r, y,     u1, v0, color, cp);
            ge_batch_add_vertex_alpha(x + w - r, y + r, u1, v1, color, cp);
            ge_batch_add_vertex_alpha(x + w,     y + r, u0, v1, color, cp);
            ge_batch_add_vertex_alpha(x + w - r, y,     u1, v0, color, cp);
            ge_batch_add_vertex_alpha(x + w,     y + r, u0, v1, color, cp);
            ge_batch_add_vertex_alpha(x + w,     y,     u0, v0, color, cp);

            /* corner BL */
            ge_batch_add_vertex_alpha(x,     y + h - r, u0, v1, color, cp);
            ge_batch_add_vertex_alpha(x,     y + h,     u0, v0, color, cp);
            ge_batch_add_vertex_alpha(x + r, y + h,     u1, v0, color, cp);
            ge_batch_add_vertex_alpha(x,     y + h - r, u0, v1, color, cp);
            ge_batch_add_vertex_alpha(x + r, y + h,     u1, v0, color, cp);
            ge_batch_add_vertex_alpha(x + r, y + h - r, u1, v1, color, cp);

            /* corner BR */
            ge_batch_add_vertex_alpha(x + w - r, y + h - r, u1, v1, color, cp);
            ge_batch_add_vertex_alpha(x + w - r, y + h,     u1, v0, color, cp);
            ge_batch_add_vertex_alpha(x + w,     y + h,     u0, v0, color, cp);
            ge_batch_add_vertex_alpha(x + w - r, y + h - r, u1, v1, color, cp);
            ge_batch_add_vertex_alpha(x + w,     y + h,     u0, v0, color, cp);
            ge_batch_add_vertex_alpha(x + w,     y + h - r, u0, v1, color, cp);
        }
    } else {
        if (r == 0) {
            int16_t lw = 1;
            /* edge top */
            ge_batch_add_vertex_shape(x, y, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y + lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y, 0, 0, 0, color, 0, false);

            /* edge bottom */
            ge_batch_add_vertex_shape(x, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y + h, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + h, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + h, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + h - lw, 0, 0, 0, color, 0, false);

            /* edge left */
            ge_batch_add_vertex_shape(x, y + lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + lw, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x, y + lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + lw, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + lw, y + lw, 0, 0, 0, color, 0, false);

            /* edge right */
            ge_batch_add_vertex_shape(x + w - lw, y + lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w - lw, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w - lw, y + lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + h - lw, 0, 0, 0, color, 0, false);
            ge_batch_add_vertex_shape(x + w, y + lw, 0, 0, 0, color, 0, false);
        } else {
            /* rounded outline - full quad fed to SDF shader to draw 4 edges as circle arcs */
            float fw = (float)w, fh = (float)h, fr = (float)r;
            float hw = fw * 0.5f, hh = fh * 0.5f;
            float pad = 2.0f;
            float x1 = (float)x - pad, y1 = (float)y - pad;
            float x2 = (float)x + fw + pad, y2 = (float)y + fh + pad;
            float px1 = -hw - pad, py1 = -hh - pad;
            float px2 = hw + pad, py2 = hh + pad;

            ge_batch_add_vertex_complex(x1, y1, px1, py1, hw, hh, fr, color, 2.0f);
            ge_batch_add_vertex_complex(x1, y2, px1, py2, hw, hh, fr, color, 2.0f);
            ge_batch_add_vertex_complex(x2, y2, px2, py2, hw, hh, fr, color, 2.0f);
            ge_batch_add_vertex_complex(x1, y1, px1, py1, hw, hh, fr, color, 2.0f);
            ge_batch_add_vertex_complex(x2, y2, px2, py2, hw, hh, fr, color, 2.0f);
            ge_batch_add_vertex_complex(x2, y1, px2, py1, hw, hh, fr, color, 2.0f);
        }
    }
}

static void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    GLBackendState *s = geogl_get_state();
    uint32_t color = s->current_color.u32;
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float w = GE_LINE_WIDTH / 2.0f;
    float px[4] = {(float)x1 + nx * w, (float)x2 + nx * w, (float)x2 - nx * w, (float)x1 - nx * w};
    float py[4] = {(float)y1 + ny * w, (float)y2 + ny * w, (float)y2 - ny * w, (float)y1 - ny * w};
    
    ge_batch_add_vertex_shape((int16_t)px[0], (int16_t)py[0], -32767, -32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[1], (int16_t)py[1], -32767, 32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[2], (int16_t)py[2], 32767, 32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[0], (int16_t)py[0], -32767, -32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[2], (int16_t)py[2], 32767, 32767, 0, color, 0, false);
    ge_batch_add_vertex_shape((int16_t)px[3], (int16_t)py[3], 32767, -32767, 0, color, 0, false);
}

__attribute__((constructor))
static void init(void) {
    gecnd_registry("bind", "internal:gl", &enabled_gl, (void*) GECND_TYPE_BOOLEAN);
    gecnd_registry("hook", "core:pre_draw", (void *)core_pre_draw, NULL);
    gecnd_registry("hook", "core:pre_tint", (void *)core_pre_tint, NULL);
    gecnd_registry("set", "backend_func:native_draw_color", (void *)native_draw_color, NULL);
    gecnd_registry("set", "backend_func:native_draw_clear", (void *)native_draw_clear, NULL);
    gecnd_registry("set", "backend_func:native_draw_rect",  (void *)native_draw_rect,  NULL);
    gecnd_registry("set", "backend_func:native_draw_line",  (void *)native_draw_line,  NULL);
}
