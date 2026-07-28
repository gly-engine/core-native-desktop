#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "gecnd.h"

/* ── face handle: FT_Face + the memory FreeType reads from (must outlive it) */

typedef struct {
    FT_Face  face;
    uint8_t *bytes;
} font_face_t;

static FT_Library s_ft = NULL;

/* ── shelf packer over the shared GAMELY_FONT_ATLAS_SIZE region ─────── */

static int16_t s_pack_x, s_pack_y, s_pack_row_h;

static bool pack_alloc(int16_t w, int16_t h, int16_t *ox, int16_t *oy) {
    if (s_pack_x + w > GAMELY_FONT_ATLAS_SIZE) {
        s_pack_x     = 0;
        s_pack_y    += s_pack_row_h;
        s_pack_row_h = 0;
    }
    if (s_pack_y + h > GAMELY_FONT_ATLAS_SIZE) return false;
    *ox = s_pack_x;
    *oy = s_pack_y;
    s_pack_x += w;
    if (h > s_pack_row_h) s_pack_row_h = h;
    return true;
}

/* ── glyph cache: (face, px_size, codepoint) -> gamely_font_glyph_t ──── */

#define GLYPH_CACHE_CAP 4096

typedef struct {
    const void          *face;
    uint8_t              px_size;
    uint32_t             codepoint;
    bool                 used;
    bool                 empty;   /* codepoint has no visible bitmap (e.g. space) */
    gamely_font_glyph_t  glyph;
} glyph_cache_entry_t;

static glyph_cache_entry_t s_cache[GLYPH_CACHE_CAP];

static uint32_t cache_hash(const void *face, uint8_t px_size, uint32_t codepoint) {
    uint32_t h = (uint32_t)(uintptr_t)face;
    h = h * 31 + px_size;
    h = h * 31 + codepoint;
    return h;
}

static glyph_cache_entry_t *cache_find_slot(const void *face, uint8_t px_size, uint32_t codepoint) {
    uint32_t idx = cache_hash(face, px_size, codepoint) % GLYPH_CACHE_CAP;
    for (uint32_t i = 0; i < GLYPH_CACHE_CAP; i++) {
        glyph_cache_entry_t *e = &s_cache[(idx + i) % GLYPH_CACHE_CAP];
        if (!e->used) return e;
        if (e->face == face && e->px_size == px_size && e->codepoint == codepoint) return e;
    }
    return NULL; /* cache full */
}

/* ── decode ───────────────────────────────────────────────────────── */

static void *ft_decode(const uint8_t *data, size_t len) {
    if (!s_ft) return NULL;

    uint8_t *copy = malloc(len);
    if (!copy) return NULL;
    memcpy(copy, data, len);

    FT_Face face;
    if (FT_New_Memory_Face(s_ft, copy, (FT_Long)len, 0, &face) != 0) {
        free(copy);
        return NULL;
    }

    font_face_t *f = malloc(sizeof(*f));
    if (!f) { FT_Done_Face(face); free(copy); return NULL; }
    f->face  = face;
    f->bytes = copy;
    return f;
}

static void ft_face_free(void *face) {
    font_face_t *f = (font_face_t *)face;
    if (!f) return;
    FT_Done_Face(f->face);
    free(f->bytes);
    free(f);
}

/* ── metrics ──────────────────────────────────────────────────────── */

static void ft_metrics(void *face, uint8_t px_size,
                       int16_t *ascent, int16_t *descent, int16_t *line_height) {
    font_face_t *f = (font_face_t *)face;
    FT_Set_Pixel_Sizes(f->face, 0, px_size);
    FT_Size_Metrics *m = &f->face->size->metrics;
    if (ascent)      *ascent      = (int16_t)(m->ascender  >> 6);
    if (descent)     *descent     = (int16_t)(-m->descender >> 6);
    if (line_height) *line_height = (int16_t)(m->height >> 6);
}

/* ── glyph: cache hit, or rasterize + pack + upload on miss ──────────── */

static bool ft_glyph(void *face, uint8_t px_size, uint32_t codepoint,
                     const gamely_font_backend_t *backend,
                     gamely_font_glyph_t *out) {
    font_face_t *f = (font_face_t *)face;

    glyph_cache_entry_t *slot = cache_find_slot(face, px_size, codepoint);
    if (slot && slot->used) {
        if (slot->empty) { memset(out, 0, sizeof(*out)); out->advance = slot->glyph.advance; return true; }
        *out = slot->glyph;
        return true;
    }

    FT_Set_Pixel_Sizes(f->face, 0, px_size);
    if (FT_Load_Char(f->face, codepoint, FT_LOAD_RENDER) != 0) return false;

    FT_GlyphSlot g   = f->face->glyph;
    int16_t      adv = (int16_t)(g->advance.x >> 6);

    gamely_font_glyph_t glyph = {0};
    glyph.advance = adv;

    bool empty = g->bitmap.width == 0 || g->bitmap.rows == 0;
    if (!empty) {
        int16_t ox, oy;
        if (!pack_alloc((int16_t)g->bitmap.width, (int16_t)g->bitmap.rows, &ox, &oy))
            return false; /* atlas full — no eviction yet, glyph silently skipped */

        glyph.atlas_x   = ox;
        glyph.atlas_y   = oy;
        glyph.w         = (int16_t)g->bitmap.width;
        glyph.h         = (int16_t)g->bitmap.rows;
        glyph.bearing_x = (int16_t)g->bitmap_left;
        glyph.bearing_y = (int16_t)g->bitmap_top;

        backend->atlas_upload(ox, oy, glyph.w, glyph.h, g->bitmap.buffer);
    }

    if (slot) {
        slot->used      = true;
        slot->empty     = empty;
        slot->face      = face;
        slot->px_size   = px_size;
        slot->codepoint = codepoint;
        slot->glyph     = glyph;
    }

    if (empty) { memset(out, 0, sizeof(*out)); out->advance = adv; }
    else       *out = glyph;
    return true;
}

/* ── registration ─────────────────────────────────────────────────── */

static const gamely_font_driver_t s_driver = {
    .decode    = ft_decode,
    .glyph     = ft_glyph,
    .metrics   = ft_metrics,
    .face_free = ft_face_free,
};

__attribute__((constructor))
static void init(void) {
    if (FT_Init_FreeType(&s_ft) != 0) {
        printf("[font-freetype] FT_Init_FreeType failed\n");
        s_ft = NULL;
    }
    gecnd_registry("set", "font_driver", (void *)&s_driver, NULL);
}
