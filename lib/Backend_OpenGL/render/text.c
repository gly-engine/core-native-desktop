#include <stdint.h>

#include "geopengl.h"
#include "gecnd/font_noto_sans.h"

/* Selection state only — all glyph rasterization/atlas/layout logic lives in
 * Daemon_Font (service_font.c + driver_freetype.c) and font_backend.c. */

static int32_t current_font_id = -1;
static int32_t default_font_id = -1;
static uint8_t current_size    = 16;

static void ensure_default_loaded(void) {
    if (default_font_id != -1) return;
    default_font_id = gamely_daemon_font_load_memory(Noto_Sans_NotoSans_Regular_ASCII_ttf, Noto_Sans_NotoSans_Regular_ASCII_ttf_len);
    gamely_daemon_font_set_family("default", default_font_id);
    if (current_font_id == -1) current_font_id = default_font_id;
}

void native_text_terminate(void) {
    gamely_daemon_font_unload_all();
    current_font_id = -1;
    default_font_id = -1;
}

static void native_text_print(int16_t x, int16_t y, const char *text) {
    if (!text) return;
    if (current_font_id == -1) ensure_default_loaded();
    if (current_font_id == -1) return;
    GLBackendState *s = geogl_get_state();
    gamely_daemon_font_draw_text(current_font_id, current_size, x, y, text, s->current_color.u32);
}

static void native_text_mensure(const char *text, int16_t *w, int16_t *h) {
    if (current_font_id == -1) ensure_default_loaded();
    if (current_font_id == -1) { if (w) *w = 0; if (h) *h = 0; return; }
    gamely_daemon_font_mensure_text(current_font_id, current_size, text, w, h);
}

static void native_text_font_size(uint8_t size) {
    current_size = size ? size : 16;
}

static void native_text_font_name(const char *name) {
    int32_t id = gamely_daemon_font_get_id_by_family(name);
    if (id != -1) current_font_id = id;
}

static void native_text_font_default(uint8_t index) {
    (void)index;
    ensure_default_loaded();
    current_font_id = default_font_id;
}

__attribute__((constructor))
static void init(void) {
    gecnd_registry("set", "backend_func:native_text_print",        (void *)native_text_print,        NULL);
    gecnd_registry("set", "backend_func:native_text_mensure",      (void *)native_text_mensure,      NULL);
    gecnd_registry("set", "backend_func:native_text_font_size",    (void *)native_text_font_size,    NULL);
    gecnd_registry("set", "backend_func:native_text_font_name",    (void *)native_text_font_name,    NULL);
    gecnd_registry("set", "backend_func:native_text_font_default", (void *)native_text_font_default, NULL);
}
