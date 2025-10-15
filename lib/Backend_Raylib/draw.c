#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "raylib.h"

//! @cond
static Color gly_current_color = {255, 255, 255, 255};
static Font gly_current_font = {0};
static bool gly_font_loaded = false;
static int gly_font_size = 16;
//! @endcond

void gly_hook_display_init(uint16_t width, uint16_t height) {
    if (!IsWindowReady()) {
        InitWindow(width, height, "GlyDisplay (Raylib)");
    }
    SetTargetFPS(60);
    gly_current_color = WHITE;
    gly_current_font = GetFontDefault();
    gly_font_loaded = false;
    gly_font_size = 16;
}

void gly_hook_display_close(void) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}

void native_draw_start(void) {
    BeginDrawing();
}

void native_draw_flush(void) {
    EndDrawing();
}

void native_draw_color(uint32_t color) {
    // color_u rgba = { color };
    gly_current_color = WHITE;
}

void native_draw_clear(uint32_t color) {
    // color_u rgba = { color };
    ClearBackground(BLACK);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (mode == 0)
        DrawRectangle(x, y, w, h, gly_current_color);
    else
        DrawRectangleLines(x, y, w, h, gly_current_color);
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    DrawLine(x1, y1, x2, y2, gly_current_color);
}

void native_text_print(int16_t x, int16_t y, const char *text) {
    if (!gly_font_loaded)
        DrawText(text, x, y, gly_font_size, gly_current_color);
    else
        DrawTextEx(gly_current_font, text, (Vector2){x, y}, gly_font_size, 0, gly_current_color);
}

void native_text_mensure(const char *text, int16_t *w, int16_t *h) {
    Vector2 size;

    if (!gly_font_loaded)
        size = MeasureTextEx(GetFontDefault(), text, gly_font_size, 0);
    else
        size = MeasureTextEx(gly_current_font, text, gly_font_size, 0);

    if (w)
        *w = (int16_t)ceilf(size.x);
    if (h)
        *h = (int16_t)ceilf(size.y);
}

void native_text_font_size(uint8_t size) {
    gly_font_size = (size > 0) ? size : 16;
}

void native_text_font_name(const char *path) {
    if (gly_font_loaded) {
        UnloadFont(gly_current_font);
        gly_font_loaded = false;
    }

    gly_current_font = LoadFont(path);
    if (gly_current_font.texture.id != 0) {
        gly_font_loaded = true;
    }
}

void native_text_font_default(int index) {
    (void)index;
    if (gly_font_loaded) {
        UnloadFont(gly_current_font);
        gly_font_loaded = false;
    }
    gly_current_font = GetFontDefault();
}

void native_text_font_previous(void) {}
