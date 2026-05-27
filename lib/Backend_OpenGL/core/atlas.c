/* Atlas manager.
 *
 * Owns two flavours of GPU atlas:
 *   - RGBA metaatlas pages (GE_ATLAS_SIZE²), shared by text, shapes and images.
 *   - YUV420P atlas triplets (Y full-res + Cb/Cr half-res) used to upload
 *     jpeg-decoded images straight as planar YUV, skipping the CPU YUV->RGB
 *     conversion and shrinking the upload to 1.5 bytes/pixel.
 *
 * Both allocators are simple shelf packers that recycle freed rectangles
 * first-fit before bumping the cursor, then spill into a new page on overflow.
 */
#include "geopengl.h"

/* ── shared texture setup ─────────────────────────────────────────── */

static void tex_linear_clamp(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* ── RGBA metaatlas ───────────────────────────────────────────────── */

void ge_atlas_create_page(int w, int h) {
    GLBackendState *s = geogl_get_state();
    GEAtlasPage page = {0};
    glGenTextures(1, &page.tex_id);
    glBindTexture(GL_TEXTURE_2D, page.tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    tex_linear_clamp();
    page.cursor_x = 0;
    page.cursor_y = 0;
    page.row_height = 0;
    page.reset_cursor_x = 0;
    page.reset_cursor_y = 0;
    page.reset_row_height = 0;
    kv_init(page.free_rects);
    kv_push(GEAtlasPage, s->atlas_pages, page);
}

void ge_atlas_alloc(int w, int h, int *page_index, int *ox, int *oy) {
    GLBackendState *s = geogl_get_state();
    int slot_w = w + 1, slot_h = h + 1;
    /* Prefer recycling a freed slot (first-fit) before bumping the cursor */
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        for (int j = 0; j < (int)kv_size(p->free_rects); j++) {
            GEAtlasRect r = p->free_rects.a[j];
            if (r.w >= slot_w && r.h >= slot_h) {
                *page_index = i;
                *ox = r.x;
                *oy = r.y;
                p->free_rects.a[j] = p->free_rects.a[--p->free_rects.n];
                return;
            }
        }
    }
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        if (p->cursor_x + slot_w > GE_ATLAS_SIZE) {
            p->cursor_x = 0; p->cursor_y += p->row_height; p->row_height = 0;
        }
        if (p->cursor_y + slot_h <= GE_ATLAS_SIZE) {
            *page_index = i;
            *ox = p->cursor_x; *oy = p->cursor_y;
            p->cursor_x += slot_w;
            if (slot_h > p->row_height) p->row_height = slot_h;
            return;
        }
    }
    int next_page = (int)kv_size(s->atlas_pages);
    ge_atlas_create_page(GE_ATLAS_SIZE, GE_ATLAS_SIZE);
    GEAtlasPage *p = &s->atlas_pages.a[next_page];
    *page_index = next_page;
    *ox = p->cursor_x; *oy = p->cursor_y;
    p->cursor_x += slot_w;
    p->row_height = slot_h;
}

void ge_atlas_free(int page_index, int ox, int oy, int w, int h) {
    GLBackendState *s = geogl_get_state();
    if (page_index < 0 || page_index >= (int)kv_size(s->atlas_pages)) return;
    GEAtlasPage *p = &s->atlas_pages.a[page_index];
    GEAtlasRect r = { ox, oy, w + 1, h + 1 };
    kv_push(GEAtlasRect, p->free_rects, r);
}

void ge_atlas_reset_images(void) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        GEAtlasPage *p = &s->atlas_pages.a[i];
        p->cursor_x   = p->reset_cursor_x;
        p->cursor_y   = p->reset_cursor_y;
        p->row_height = p->reset_row_height;
        p->free_rects.n = 0;
    }
    ge_atlas_yuv_reset();
}

/* ── YUV420P atlas ────────────────────────────────────────────────── */

/* Round a dimension up to the next even value so the half-res chroma plane
 * never straddles a texel boundary. */
static inline int even_up(int v) { return (v + 1) & ~1; }

static void create_yuv_atlas_page(GLBackendState *s) {
    GEYuvAtlasPage page = {0};
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

    page.cursor_x = 0;
    page.cursor_y = 0;
    page.row_height = 0;
    kv_init(page.free_rects);
    kv_push(GEYuvAtlasPage, s->yuv_atlas_pages, page);
}

void ge_atlas_yuv_alloc(int w, int h, int *page_index, int *ox, int *oy) {
    GLBackendState *s = geogl_get_state();
    /* Even-align the request and pad by 2 so the next slot stays even too. */
    int slot_w = even_up(w) + 2, slot_h = even_up(h) + 2;

    for (int i = 0; i < (int)kv_size(s->yuv_atlas_pages); i++) {
        GEYuvAtlasPage *p = &s->yuv_atlas_pages.a[i];
        for (int j = 0; j < (int)kv_size(p->free_rects); j++) {
            GEAtlasRect r = p->free_rects.a[j];
            if (r.w >= slot_w && r.h >= slot_h) {
                *page_index = i;
                *ox = r.x;
                *oy = r.y;
                p->free_rects.a[j] = p->free_rects.a[--p->free_rects.n];
                return;
            }
        }
    }
    for (int i = 0; i < (int)kv_size(s->yuv_atlas_pages); i++) {
        GEYuvAtlasPage *p = &s->yuv_atlas_pages.a[i];
        if (p->cursor_x + slot_w > GE_YUV_ATLAS_SIZE) {
            p->cursor_x = 0; p->cursor_y += p->row_height; p->row_height = 0;
        }
        if (p->cursor_y + slot_h <= GE_YUV_ATLAS_SIZE) {
            *page_index = i;
            *ox = p->cursor_x; *oy = p->cursor_y;
            p->cursor_x += slot_w;
            if (slot_h > p->row_height) p->row_height = slot_h;
            return;
        }
    }
    int next_page = (int)kv_size(s->yuv_atlas_pages);
    create_yuv_atlas_page(s);
    GEYuvAtlasPage *p = &s->yuv_atlas_pages.a[next_page];
    *page_index = next_page;
    *ox = p->cursor_x; *oy = p->cursor_y;
    p->cursor_x += slot_w;
    p->row_height = slot_h;
}

void ge_atlas_yuv_free(int page_index, int ox, int oy, int w, int h) {
    GLBackendState *s = geogl_get_state();
    if (page_index < 0 || page_index >= (int)kv_size(s->yuv_atlas_pages)) return;
    GEYuvAtlasPage *p = &s->yuv_atlas_pages.a[page_index];
    GEAtlasRect r = { ox, oy, even_up(w) + 2, even_up(h) + 2 };
    kv_push(GEAtlasRect, p->free_rects, r);
}

void ge_atlas_yuv_reset(void) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->yuv_atlas_pages); i++) {
        GEYuvAtlasPage *p = &s->yuv_atlas_pages.a[i];
        p->cursor_x   = 0;
        p->cursor_y   = 0;
        p->row_height = 0;
        p->free_rects.n = 0;
    }
}

/* ── lifecycle ────────────────────────────────────────────────────── */

void ge_atlas_init(void) {
    GLBackendState *s = geogl_get_state();
    kv_init(s->atlas_pages);
    kv_init(s->yuv_atlas_pages);
}

void ge_atlas_terminate(void) {
    GLBackendState *s = geogl_get_state();
    for (int i = 0; i < (int)kv_size(s->atlas_pages); i++) {
        glDeleteTextures(1, &s->atlas_pages.a[i].tex_id);
        kv_destroy(s->atlas_pages.a[i].free_rects);
    }
    kv_destroy(s->atlas_pages);

    for (int i = 0; i < (int)kv_size(s->yuv_atlas_pages); i++) {
        GEYuvAtlasPage *p = &s->yuv_atlas_pages.a[i];
        glDeleteTextures(1, &p->tex_y);
        glDeleteTextures(1, &p->tex_u);
        glDeleteTextures(1, &p->tex_v);
        kv_destroy(p->free_rects);
    }
    kv_destroy(s->yuv_atlas_pages);
}
