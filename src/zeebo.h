#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

#ifndef TEST
#include "SDL/include/SDL.h"
#include "SDL_ttf/SDL_ttf.h"
#endif 

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

#include "curl/include/curl/curl.h"

#ifndef NOT_USE_GETOPT
#include <unistd.h>
#endif

#ifndef TEST
#define PRIVATE static
#else
#define PRIVATE extern
#endif

typedef enum {
    KERNEL_EVENT_PRE_INIT,
    KERNEL_EVENT_INIT,
    KERNEL_EVENT_POST_INIT,
    KERNEL_EVENT_PRE_UPDATE,
    KERNEL_EVENT_PRE_TICKET,
    KERNEL_EVENT_TICKET,
    KERNEL_EVENT_POST_TICKET,
    KERNEL_EVENT_UPDATE,
    KERNEL_EVENT_POST_UPDATE,
    KERNEL_EVENT_PRE_DRAW,
    KERNEL_EVENT_DRAW,
    KERNEL_EVENT_POST_DRAW,
    KERNEL_EVENT_PRE_EXIT,
    KERNEL_EVENT_EXIT,
    KERNEL_EVENT_POST_EXIT,
    KERNEL_EVENT_COUNT
} kernel_event_t;

typedef struct {
    bool resized;
    bool hardware;
    uint8_t fullscreen;
    uint16_t width;
    uint16_t height;
    char *media;
    char *game;
    char *engine;
} kernel_options_t;

typedef struct {
    unsigned long ticks_60fps;
    unsigned long ticks;
    unsigned long dt;
} kernel_time_t;

extern lua_State *const lua();
extern kernel_time_t kernel_time;
extern kernel_options_t kernel_option;

void kernel_init(int argc, char* argv[]);
void kernel_update();
void kernel_exit();

//! @file src/engine_event.c
void engine_install();
void engine_keypress(const char *const , uint8_t);

//! @file src/ffmpeg.c
void ffmpeg_install();

//! @file src/kernel_error.c
void kernel_add_error(const char*);
bool kernel_has_error();
const char *const kernel_get_error();

//! @file src/kernel_event.c
void kernel_event_install(kernel_event_t, void*);
void kernel_event_callback(kernel_event_t, kernel_event_t);

//! @file src/kernel_runtime.c
bool kernel_runtime_online();
void kernel_runtime_quit();
int kernel_runtime_get_status();

//! @file src/lua.c
void lua_install();

void native_draw_install();
void native_http_install();
void native_json_install();

//! @file src/sdl_api.c
void sdl_set_title(char* new_title);
char *const sdl_get_title();

//! @file src/sdl_primitives.c
void sdl_install();

//! @file src/sdl_text.c
void sdl_text_font_size(uint32_t font_size);
void sdl_text_print(uint8_t mode, int x, int y, const char *text, int *const width, int *const height);

//! @file src/sdl_primitives.c
#ifndef TEST
SDL_Color sdl_get_color();
#endif
void sdl_draw_start();
void sdl_draw_flush();
void sdl_draw_clear(uint32_t c, double x, double y, double w, double h);
void sdl_draw_color(uint32_t c);
void sdl_draw_rect(uint8_t mode, double x, double y, double w, double h);
void sdl_draw_line(double x1, double y1, double x2, double y2);
