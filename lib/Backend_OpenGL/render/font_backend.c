#include <stdint.h>

#include "geopengl.h"

/* "Dumb" GL half of Daemon_Font: uploads alpha8 glyph bitmaps and draws quads
 * into the shared atlas page's font-reserved region (top-left 1024x1024,
 * see pipeline/core.c). All glyph rasterization/caching/packing lives in
 * driver_freetype.c. */

static void gl_atlas_upload(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *bitmap) {
    if (w <= 0 || h <= 0) return;
    GLBackendState *s = geogl_get_state();
    ge_pipeline_flush_primitives();
    glBindTexture(GL_TEXTURE_2D, s->atlas_pages.a[s->corner_page_index].tex_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    s->atlas_dirty = true;
}

static void gl_draw_quad(int16_t dst_x, int16_t dst_y, int16_t dst_w, int16_t dst_h,
                          float u0, float v0, float u1, float v1, uint32_t rgba) {
    GLBackendState *s    = geogl_get_state();
    int             page = s->corner_page_index;

    /* the font region is GE_FONT_ATLAS_SIZE logical px inside the GE_ATLAS_SIZE page */
    float su0 = u0 * (float)GE_FONT_ATLAS_SIZE / (float)GE_ATLAS_SIZE;
    float sv0 = v0 * (float)GE_FONT_ATLAS_SIZE / (float)GE_ATLAS_SIZE;
    float su1 = u1 * (float)GE_FONT_ATLAS_SIZE / (float)GE_ATLAS_SIZE;
    float sv1 = v1 * (float)GE_FONT_ATLAS_SIZE / (float)GE_ATLAS_SIZE;

    int16_t x0 = dst_x, y0 = dst_y;
    int16_t x1 = (int16_t)(dst_x + dst_w), y1 = (int16_t)(dst_y + dst_h);

    ge_batch_add_vertex_alpha(x0, y0, su0, sv0, rgba, page);
    ge_batch_add_vertex_alpha(x1, y0, su1, sv0, rgba, page);
    ge_batch_add_vertex_alpha(x1, y1, su1, sv1, rgba, page);
    ge_batch_add_vertex_alpha(x0, y0, su0, sv0, rgba, page);
    ge_batch_add_vertex_alpha(x1, y1, su1, sv1, rgba, page);
    ge_batch_add_vertex_alpha(x0, y1, su0, sv1, rgba, page);
}

static void gl_unload_all(void) {
    /* the atlas page itself persists for the app's lifetime; nothing owned
     * per-font to release here (no eviction yet — see docs/design-daemon-font.md) */
}

static const gamely_font_backend_t s_backend = {
    .atlas_upload = gl_atlas_upload,
    .draw_quad    = gl_draw_quad,
    .unload_all   = gl_unload_all,
};

void gamely_daemon_font_opengl_register(void) {
    gecnd_registry("set", "font_backend", (void *)&s_backend, NULL);
}
