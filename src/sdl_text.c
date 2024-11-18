#include "font/Noto_Sans/notosans_regular.h"
#include "zeebo.h"

//! @cond
extern SDL_Renderer *renderer;
static TTF_Font *font[80];
static TTF_Font *font_current;
//! @endcond

/**
 * @throw kernel_error Failed to load font
 */
void sdl_text_font_size(uint32_t font_size) {
    if (font_size < 3) {
        font_size = 3;
    }
    if (font_size > 82) {
        font_size = 82;
    }

    uint32_t index = font_size - 3;

    if (font[index] == NULL) {
        SDL_RWops *rw = SDL_RWFromConstMem(notosans_regular_ttf, notosans_regular_ttf_len);
        font[index] = TTF_OpenFontRW(rw, 1, font_size);
    }

    if (font[index] == NULL) {
        kernel_add_error("Failed to load font");
        kernel_add_error(SDL_GetError());
    } else {
        font_current = font[index];
    }
}

/**
 * @details If you pass an integer pointer to @c width and @c height
 * , they will be set to the text size.
 * @param [in] mode
 * @param [in] x
 * @param [in] y
 * @param [in] text
 * @param [out] width
 * @param [out] height
 * @li mode 0: print
 * @li mode 1: not print (only mensure)
 * @throw kernel_error Font is not set
 * @throw kernel_error Failed to create text surface
 * @throw kernel_error Failed to create text texture
 */
void sdl_text_print(uint8_t mode, int x, int y, const char *text, int *const width, int *const height) {
    int textWidth = 0;
    int textHeight = 0;
    SDL_Surface *textSurface = NULL;
    SDL_Texture *textTexture = NULL;

    do {
        if (text == NULL) {
            break;
        }

        if (font_current == NULL) {
            kernel_add_error("Font is not set");
            break;
        }

        textSurface = TTF_RenderText_Solid(font_current, text, sdl_get_color());
        if (textSurface == NULL) {
            kernel_add_error("Failed to create text surface:");
            kernel_add_error(TTF_GetError());
            break;
        }

        textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
        if (textTexture == NULL) {
            kernel_add_error("Failed to create text texture:");
            kernel_add_error(SDL_GetError());
            break;
        }

        SDL_QueryTexture(textTexture, NULL, NULL, &textWidth, &textHeight);
        if (mode == 0) {
            SDL_Rect destRect = {x, y, textWidth, textHeight};
            SDL_RenderCopy(renderer, textTexture, NULL, &destRect);
        }
    } while (0);

    if (textSurface) {
        SDL_FreeSurface(textSurface);
    }
    if (textTexture) {
        SDL_DestroyTexture(textTexture);
    }

    if (width && height) {
        *width = textWidth;
        *height = textHeight;
    }
}
