#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "kvec.h"

static SDL_Window *gly_window = NULL;
static SDL_Renderer *gly_renderer = NULL;
static SDL_Color gly_current_color = {255, 255, 255, 255};
static TTF_Font *gly_current_font = NULL;
static bool gly_font_loaded = false;
static int gly_font_size = 16;
static kvec_t(SDL_Texture*) gly_textures;

void gly_hook_display_init(uint16_t width, uint16_t height) {
     if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return;
    }

    if (TTF_Init() == -1) {
        fprintf(stderr, "TTF_Init Error: %s\n", TTF_GetError());
        return;
    }

    gly_window = SDL_CreateWindow("GlyDisplay (SDL2)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
    if (!gly_window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        return;
    }

    gly_renderer = SDL_CreateRenderer(gly_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!gly_renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        return;
    }

    gly_current_color = (SDL_Color){255, 255, 255, 255};
    gly_current_font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", gly_font_size);
    gly_font_loaded = (gly_current_font != NULL);
    kv_init(gly_textures);
}

void gly_hook_display_close(void) {
    for (size_t i = 0; i < kv_size(gly_textures); i++) {
        if (kv_A(gly_textures, i)) SDL_DestroyTexture(kv_A(gly_textures, i));
    }
    kv_destroy(gly_textures);

    if (gly_current_font) TTF_CloseFont(gly_current_font);
    if (gly_renderer) SDL_DestroyRenderer(gly_renderer);
    if (gly_window) SDL_DestroyWindow(gly_window);

    TTF_Quit();
    SDL_Quit();
}

void gly_hook_display_fps(uint8_t fps) {
    (void)fps;
}

void gly_hook_display_dt(int16_t* delta_time) {
    static uint32_t last_ticks = 0;
    uint32_t now = SDL_GetTicks();
    *delta_time = (int16_t)(now - last_ticks);
    last_ticks = now;
}

void gly_hook_should_close(bool *should_close) {
    SDL_Event e;
    *should_close = false;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) *should_close = true;
    }
}

static inline SDL_Color color_from_u32(uint32_t c) {
    return (SDL_Color){(c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF};
}

void native_draw_start(void) {
    SDL_SetRenderDrawColor(gly_renderer, gly_current_color.r, gly_current_color.g, gly_current_color.b, gly_current_color.a);
    SDL_RenderClear(gly_renderer);
}

void native_draw_flush(void) {
    SDL_RenderPresent(gly_renderer);
}

void native_draw_color(uint32_t color) {
    gly_current_color = color_from_u32(color);
    SDL_SetRenderDrawColor(gly_renderer, gly_current_color.r, gly_current_color.g, gly_current_color.b, gly_current_color.a);
}

void native_draw_clear(uint32_t color) {
    SDL_Color c = color_from_u32(color);
    SDL_SetRenderDrawColor(gly_renderer, c.r, c.g, c.b, c.a);
    SDL_RenderClear(gly_renderer);
}

void native_draw_rect(uint8_t mode, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r) {
    SDL_Rect rect = {x, y, w, h};

    if (mode == 0) {
        SDL_RenderFillRect(gly_renderer, &rect);
    } else {
        SDL_RenderDrawRect(gly_renderer, &rect);
    }
}

void native_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    SDL_RenderDrawLine(gly_renderer, x1, y1, x2, y2);
}

void native_text_print(int16_t x, int16_t y, const char *text) {
    if (!gly_font_loaded) return;

    SDL_Surface *surf = TTF_RenderUTF8_Blended(gly_current_font, text, gly_current_color);
    if (!surf) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(gly_renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    SDL_RenderCopy(gly_renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

void native_text_mensure(const char *text, int16_t *w, int16_t *h) {
    if (!gly_font_loaded) return;
    int tw, th;
    if (TTF_SizeUTF8(gly_current_font, text, &tw, &th) == 0) {
        if (w) *w = (int16_t)tw;
        if (h) *h = (int16_t)th;
    }
}

void native_text_font_size(uint8_t size) {
    gly_font_size = (size > 0) ? size : 16;
    if (gly_current_font) {
        TTF_CloseFont(gly_current_font);
        gly_current_font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", gly_font_size);
        gly_font_loaded = (gly_current_font != NULL);
    }
}

void native_text_font_name(const char *path) {
    if (gly_font_loaded) TTF_CloseFont(gly_current_font);
    gly_current_font = TTF_OpenFont(path, gly_font_size);
    gly_font_loaded = (gly_current_font != NULL);
}

void native_text_font_default(uint8_t index) {
    (void)index;
    if (gly_font_loaded) TTF_CloseFont(gly_current_font);
    gly_current_font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", gly_font_size);
    gly_font_loaded = (gly_current_font != NULL);
}

void native_image_load(const char *path, int32_t image_id, bool *success) {
    SDL_Surface *surf = SDL_LoadBMP(path); // só BMP por enquanto
    if (!surf) {
        if (success) *success = false;
        return;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(gly_renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        if (success) *success = false;
        return;
    }

    size_t index = image_id - 1;
    while (kv_size(gly_textures) <= index) kv_push(SDL_Texture*, gly_textures, NULL);

    if (kv_A(gly_textures, index)) SDL_DestroyTexture(kv_A(gly_textures, index));
    kv_A(gly_textures, index) = tex;

    if (success) *success = true;
}

void native_image_draw(int32_t image_id, int16_t x, int16_t y) {
    size_t index = image_id - 1;
    if (image_id > 0 && kv_size(gly_textures) > index && kv_A(gly_textures, index)) {
        SDL_Texture *tex = kv_A(gly_textures, index);
        int w, h;
        SDL_QueryTexture(tex, NULL, NULL, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(gly_renderer, tex, NULL, &dst);
    }
}

void native_image_unload(int32_t image_id, bool *const success) {
    size_t index = image_id - 1;
    if (image_id > 0 && kv_size(gly_textures) > index && kv_A(gly_textures, index)) {
        SDL_DestroyTexture(kv_A(gly_textures, index));
        kv_A(gly_textures, index) = NULL;
        if (success) *success = true;
    }
}

void native_image_mensure(int32_t image_id, int16_t *w, int16_t *h) {
    size_t index = image_id - 1;
    if (image_id > 0 && kv_size(gly_textures) > index && kv_A(gly_textures, index)) {
        SDL_Texture *tex = kv_A(gly_textures, index);
        int tw, th;
        SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
        if (w) *w = tw;
        if (h) *h = th;
    }
}
