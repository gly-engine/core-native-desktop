#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "raylib.h"

#include "gecnd.h"
#include "gehook.h"
#include "kvec.h"

//! @cond
static Color gly_current_color = {255, 255, 255, 255};
static Font gly_current_font = {0};
static bool gly_font_loaded = false;
static int gly_font_size = 16;
static kvec_t(Texture2D) gly_textures;
//! @endcond

#include <stdio.h>
void gly_hook_display_init(uint16_t width, uint16_t height) {
    SetTraceLogLevel(5);
    if (!IsWindowReady()) {
        InitWindow(width, height, "GlyDisplay (Raylib)");
    }
    SetTargetFPS(60);
    SetExitKey(0);
    gly_current_color = WHITE;
    gly_current_font = GetFontDefault();
    gly_font_loaded = false;
    gly_font_size = 16;
    kv_init(gly_textures);
}

void gly_hook_display_fps(uint8_t fps) {
    if (fps == 0) {
        ClearWindowState(FLAG_VSYNC_HINT);
    }
    SetTargetFPS(fps);
}

void gly_hook_display_dt(int16_t* delta_time) {
    *delta_time = (int16_t) (int32_t) (GetFrameTime() * 1000.0f);
}

void gly_hook_should_close(bool *should_close) {
    *should_close = WindowShouldClose();
}

void gly_hook_display_close(void) {
    for (size_t i = 0; i < kv_size(gly_textures); i++) {
        if (kv_A(gly_textures, i).id > 0) {
            UnloadTexture(kv_A(gly_textures, i));
        }
    }
    kv_destroy(gly_textures);

    if (IsWindowReady()) {
        CloseWindow();
    }
}

static inline Color color_from_u32(uint32_t c) {
    return (Color){(c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF};
}

void native_draw_start(void) {
    BeginDrawing();
}

void native_draw_flush(void) {
}

void native_draw_finish(void) {
    EndDrawing();
}

void native_draw_color(uint32_t color) {
    gly_current_color = color_from_u32(color);
}

void native_draw_clear(uint32_t color) {
    ClearBackground(color_from_u32(color));
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    /// @todo fix radius in raylib backend
    float radius = r;

    if (radius > 0) {
        float maxRadius = fminf((float) w / 2.0f, (float) h / 2.0f);
        if (radius > maxRadius) radius = maxRadius;
        radius = radius / maxRadius;
    }

    if (mode == 0 && r == 0) {
        DrawRectangle(x, y, w, h, gly_current_color);
    }
    else if (mode == 1 && r == 0) {
      DrawRectangleLines(x, y, w, h, gly_current_color);
    }
    else if (mode == 0 && r > 0) {
        Rectangle shape = { x, y, w, h };
        DrawRectangleRounded(shape, radius, 32, gly_current_color);
    }
    else if (mode == 1 && r > 0) {
        Rectangle shape = { x, y, w, h };
        DrawRectangleRoundedLines(shape, radius, 32, gly_current_color);
    } 
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
    /// @todo correct use font mensure?
    if (!gly_font_loaded)
        size = MeasureTextEx(GetFontDefault(), text, gly_font_size, 2);
    else
        size = MeasureTextEx(gly_current_font, text, gly_font_size, 2);

    if (w && h) {
        *w = (int16_t)ceilf(size.x);
        *h = (int16_t)ceilf(size.y);
    }
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

void native_text_font_default(uint8_t index) {
    (void)index;
    if (gly_font_loaded) {
        UnloadFont(gly_current_font);
        gly_font_loaded = false;
    }
    gly_current_font = GetFontDefault();
}

void native_text_font_previous(void) {}

void native_image_load(const char *path, int32_t image_id, bool *success) {
    Texture2D texture = LoadTexture(path);
    if (texture.id == 0) {
        if (success)
            *success = false;
        return;
    }

    size_t index = image_id - 1;

    while (kv_size(gly_textures) <= index) {
        kv_push(Texture2D, gly_textures, (Texture2D){0});
    }

    if (kv_A(gly_textures, index).id != 0) {
        UnloadTexture(kv_A(gly_textures, index));
    }

    kv_A(gly_textures, index) = texture;
    if (success)
        *success = true;
}

void native_image_draw(int32_t image_id, int16_t x, int16_t y) {
    size_t index = image_id - 1;
    if (image_id > 0 && kv_size(gly_textures) > index) {
        Texture2D texture = kv_A(gly_textures, index);
        if (texture.id != 0) {
            DrawTexture(texture, x, y, WHITE);
        }
    }
}

void native_image_mensure(int32_t image_id, int16_t *w, int16_t *h) {
    size_t index = image_id - 1;
    if (image_id > 0 && kv_size(gly_textures) > index) {
        Texture2D texture = kv_A(gly_textures, index);
        if (texture.id != 0 && w && h) {
            *w = texture.width;
            *h = texture.height;
        }
    }
}

void native_image_unload(int32_t image_id, bool *const success) {
    (void) success;
    printf("TODO: implements native_image_unload! (%d)\n", image_id);
}