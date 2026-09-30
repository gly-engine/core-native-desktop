/* Atlas manager.
 *
 * One unified page list (`atlas_pages`), each page tagged with a color format
 * and an opaque flag. The texture handle(s) live in an anonymous union:
 *   - rgba8888 / rgba5551 : a single GL texture, shelf-allocated.
 *   - yuv420p             : Y full-res + Cb/Cr half-res, shelf-allocated.
 *   - etc1                : a vec of standalone textures (ownership only).
 *
 * The shelf allocator recycles freed rectangles first-fit before bumping the
 * cursor, then spills into a new page on overflow. Pages are bucketed by
 * (format, opaque) so the flush can iterate opaque pages then transparent.
 */
#include "geopengl.h"

#include <stdlib.h>

/* ── format table ─────────────────────────────────────────────────── */

typedef struct {
    GLint  internalformat;
    GLenum format;
    GLenum type;
    bool   planar_yuv;   /* 3-plane Y/Cb/Cr instead of one texture */
    bool   even_align;   /* round slot to even so half-res chroma divides */
} fmt_info_t;

static fmt_info_t fmt_info(GECNDColorFormat f) {
    switch (f) {
    case GECND_PIX_FMT_RGBA5551:
        return (fmt_info_t){ GL_RGBA, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, false, false };
    case GECND_PIX_FMT_YUV420P:
        return (fmt_info_t){ GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE, true, true };
    case GECND_PIX_FMT_ALPHA8:
        return (fmt_info_t){ GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE, false, false };
    case GECND_PIX_FMT_RGBA8888:
    default:
        return (fmt_info_t){ GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, false, false };
    }
}

/* ── shared texture setup ─────────────────────────────────────────── */

static void tex_linear_clamp(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* Round a dimension up to the next even value (YUV chroma alignment). */
static inline int even_up(int v) { return (v + 1) & ~1; }

/* ── page creation ────────────────────────────────────────────────── */

int ge_atlas_create_page(GECNDColorFormat fmt, bool opaque) {
    GLBackendState *s = geogl_get_state();
    fmt_info_t fi = fmt_info(fmt);

    GEAtlasPage page = {0};
    page.color_format = fmt;
    page.is_opaque    = opaque;

    if (fi.planar_yuv) {
        glGenTextures(1, &page.tex_y);
        glGenTextures(1, &page.tex_u);
        glGenTextures(1, &page.tex_v);
        glBindTexture(GL_TEXTURE_2D, page.tex_y);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, GE_YUV_ATLAS_SIZE, GE_YUV_ATLAS_SIZE,
                     0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
        tex_linear_clamp();
        glBindTexture(GL_TEXTURE_2D, page.tex_u);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, GE_YUV_CHROMA_SIZE, GE_YUV_CHROMA_SIZE,
                     0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
        tex_linear_clamp();
        glBindTexture(GL_TEXTURE_2D, page.tex_v);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, GE_YUV_CHROMA_SIZE, GE_YUV_CHROMA_SIZE,
                     0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
        tex_linear_clamp();
    } else {
        void *data = fmt == GECND_PIX_FMT_ALPHA8
            ? calloc((size_t)GE_ATLAS_SIZE * GE_ATLAS_SIZE, 1)
            : NULL;
        glGenTextures(1, &page.tex_id);
        glBindTexture(GL_TEXTURE_2D, page.tex_id);
        glTexImage2D(GL_TEXTURE_2D, 0, fi.internalformat, GE_ATLAS_SIZE, GE_ATLAS_SIZE,
                     0, fi.format, fi.type, data);
        free(data);
        tex_linear_clamp();
    }

    kv_init(page.free_rects);
    int idx = (int)kv_size(s->atlas_pages);
    kv_push(GEAtlasPage, s->atlas_pages, page);
    return idx;
}

/* ── unified shelf allocator (atlased formats) ────────────────────── */

void ge_atlas_acquire(GECNDColorFormat fmt, bool opaque, int w, int h,
                      int *page_index, int *ox, int *oy) {
    GLBackendState *s = geogl_get_state();
    fmt_info_t fi = fmt_info(fmt);
    int slot_w, slot_h;
    if (fi.even_align) { slot_w = even_up(w) + 2; slot_h = even_up(h) + 2; }
    else               { slot_w = w + 1;          slot_h = h + 1;          }

    /* Recycle a freed slot first (only on matching pages). */
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        if (p->color_format != fmt || p->is_opaque != opaque) continue;
        for (int j = 0; j < (int)kv_size(p->free_rects); j++) {
            GEAtlasRect r = p->free_rects.a[j];
            if (r.w >= slot_w && r.h >= slot_h) {
                *page_index = i; *ox = r.x; *oy = r.y;
                p->free_rects.a[j] = p->free_rects.a[--p->free_rects.n];
                return;
            }
        }
    }
    /* Bump the cursor on a matching page. */
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        if (p->color_format != fmt || p->is_opaque != opaque) continue;
        if (p->cursor_x + slot_w > GE_ATLAS_SIZE) {
            p->cursor_x = 0; p->cursor_y += p->row_height; p->row_height = 0;
        }
        if (p->cursor_y + slot_h <= GE_ATLAS_SIZE) {
            *page_index = i; *ox = p->cursor_x; *oy = p->cursor_y;
            p->cursor_x += slot_w;
            if (slot_h > p->row_height) p->row_height = slot_h;
            return;
        }
    }
    /* Spill into a fresh page. */
    int next_page = ge_atlas_create_page(fmt, opaque);
    GEAtlasPage *p = &s->atlas_pages.a[next_page];
    *page_index = next_page;
    *ox = p->cursor_x; *oy = p->cursor_y;
    p->cursor_x += slot_w;
    p->row_height = slot_h;
}

void ge_atlas_release(int page_index, int ox, int oy, int w, int h) {
    GLBackendState *s = geogl_get_state();
    if (page_index < 0 || page_index >= (int)kv_size(s->atlas_pages)) return;
    GEAtlasPage *p = &s->atlas_pages.a[page_index];
    fmt_info_t fi = fmt_info(p->color_format);
    int slot_w = fi.even_align ? even_up(w) + 2 : w + 1;
    int slot_h = fi.even_align ? even_up(h) + 2 : h + 1;
    GEAtlasRect r = { ox, oy, slot_w, slot_h };
    kv_push(GEAtlasRect, p->free_rects, r);
}

void ge_atlas_reset_images(void) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        if (p->color_format == GECND_PIX_FMT_ETC1) continue;
        p->cursor_x   = p->reset_cursor_x;
        p->cursor_y   = p->reset_cursor_y;
        p->row_height = p->reset_row_height;
        p->free_rects.n = 0;
    }
}

/* ── ETC1 ownership (standalone textures, bound by raw id) ─────────── */

static GEAtlasPage *etc1_page(GLBackendState *s) {
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++)
        if (s->atlas_pages.a[i].color_format == GECND_PIX_FMT_ETC1)
            return &s->atlas_pages.a[i];
    GEAtlasPage page = {0};
    page.color_format = GECND_PIX_FMT_ETC1;
    page.is_opaque    = true;
    kv_init(page.etc1_texs);
    kv_init(page.free_rects);
    kv_push(GEAtlasPage, s->atlas_pages, page);
    return &s->atlas_pages.a[kv_size(s->atlas_pages) - 1];
}

void ge_atlas_etc1_add(GLuint tex) {
    GEAtlasPage *p = etc1_page(geogl_get_state());
    kv_push(GLuint, p->etc1_texs, tex);
}

void ge_atlas_etc1_remove(GLuint tex) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        if (p->color_format != GECND_PIX_FMT_ETC1) continue;
        for (int j = 0; j < (int)kv_size(p->etc1_texs); j++) {
            if (p->etc1_texs.a[j] == tex) {
                p->etc1_texs.a[j] = p->etc1_texs.a[--p->etc1_texs.n];
                glDeleteTextures(1, &tex);
                return;
            }
        }
    }
}

void ge_atlas_etc1_clear(void) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        if (p->color_format != GECND_PIX_FMT_ETC1) continue;
        for (int j = 0; j < (int)kv_size(p->etc1_texs); j++)
            glDeleteTextures(1, &p->etc1_texs.a[j]);
        p->etc1_texs.n = 0;
    }
}

/* ── lifecycle ────────────────────────────────────────────────────── */

void ge_atlas_init(void) {
    GLBackendState *s = geogl_get_state();
    kv_init(s->atlas_pages);
}

void ge_atlas_terminate(void) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        switch (p->color_format) {
        case GECND_PIX_FMT_YUV420P:
            glDeleteTextures(1, &p->tex_y);
            glDeleteTextures(1, &p->tex_u);
            glDeleteTextures(1, &p->tex_v);
            break;
        case GECND_PIX_FMT_ETC1:
            for (int j = 0; j < (int)kv_size(p->etc1_texs); j++)
                glDeleteTextures(1, &p->etc1_texs.a[j]);
            kv_destroy(p->etc1_texs);
            break;
        default:
            glDeleteTextures(1, &p->tex_id);
            break;
        }
        kv_destroy(p->free_rects);
    }
    kv_destroy(s->atlas_pages);
}
