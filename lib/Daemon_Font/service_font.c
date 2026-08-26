#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gecnd.h"

#define FONT_CAP     32
#define FAMILY_CAP   32

/* ── font entry ───────────────────────────────────────────────────── */

typedef struct {
    int32_t             id;
    char               *url;
    gamely_font_state_t state;
    void               *face;
    char               *error_msg;
    int                 active;
} font_entry_t;

typedef struct {
    char    name[32];
    int32_t id;
    int     active;
} font_family_t;

static font_entry_t  s_fonts[FONT_CAP];
static font_family_t s_families[FAMILY_CAP];
static int32_t       s_next_id = 1;

/* ── entry helpers ────────────────────────────────────────────────── */

static font_entry_t *find_by_url(const char *url) {
    for (int i = 0; i < FONT_CAP; i++)
        if (s_fonts[i].active && s_fonts[i].url && strcmp(s_fonts[i].url, url) == 0)
            return &s_fonts[i];
    return NULL;
}

static font_entry_t *find_by_id(int32_t id) {
    for (int i = 0; i < FONT_CAP; i++)
        if (s_fonts[i].active && s_fonts[i].id == id)
            return &s_fonts[i];
    return NULL;
}

static font_entry_t *alloc_entry(void) {
    for (int i = 0; i < FONT_CAP; i++)
        if (!s_fonts[i].active) return &s_fonts[i];
    return NULL;
}

static void entry_free(font_entry_t *e) {
    free(e->url);
    free(e->error_msg);
    memset(e, 0, sizeof(*e));
}

static void set_error(font_entry_t *e, const char *msg) {
    free(e->error_msg);
    e->error_msg = msg ? strdup(msg) : NULL;
    e->state     = GLY_FONT_ERROR;
    printf("[font] error id=%d '%s': %s\n", e->id, e->url ? e->url : "(memory)", msg ? msg : "");
}

/* ── driver / backend lookup ──────────────────────────────────────── */

static const gamely_font_driver_t *find_driver(void) {
    void *cbs = NULL;
    gecnd_registry("get", "font_driver", (void *)&cbs, NULL);
    return (const gamely_font_driver_t *)cbs;
}

static const gamely_font_backend_t *find_backend(void) {
    void *cbs = NULL;
    gecnd_registry("get", "font_backend", (void *)&cbs, NULL);
    return (const gamely_font_backend_t *)cbs;
}

/* ── family alias table ──────────────────────────────────────────── */

void gamely_daemon_font_set_family(const char *name, int32_t id) {
    if (!name) return;
    for (int i = 0; i < FAMILY_CAP; i++) {
        if (s_families[i].active && strcmp(s_families[i].name, name) == 0) {
            s_families[i].id = id;
            return;
        }
    }
    for (int i = 0; i < FAMILY_CAP; i++) {
        if (!s_families[i].active) {
            strncpy(s_families[i].name, name, sizeof(s_families[i].name) - 1);
            s_families[i].id     = id;
            s_families[i].active = 1;
            return;
        }
    }
    printf("[font] no free family slots for '%s'\n", name);
}

int32_t gamely_daemon_font_get_id_by_family(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < FAMILY_CAP; i++)
        if (s_families[i].active && strcmp(s_families[i].name, name) == 0)
            return s_families[i].id;
    return -1;
}

/* ── resolver dispatch (mirrors Daemon_Img/service_img.c) ────────── */

typedef struct { gamely_font_schema_cb cb; const char *key; } resolver_pick_t;

static void resolver_pick(const char *key, void *value, void *usr) {
    resolver_pick_t *p = (resolver_pick_t *)usr;
    p->cb  = (gamely_font_schema_cb)value;
    p->key = key;
}

static void on_fetch(const uint8_t *data, size_t len, const char *hint, void *usr);

static void dispatch_url(font_entry_t *e, const char *url) {
    resolver_pick_t pick = { NULL, NULL };
    char *key = malloc(strlen(url) + sizeof("font_resolver:()"));
    if (key) {
        sprintf(key, "font_resolver:(%s)", url);
        gecnd_registry("get", key, (void *)resolver_pick, &pick);
        free(key);
    }
    if (!pick.cb) {
        printf("[font] no resolver for '%s'\n", url);
        set_error(e, "no schema handler");
        return;
    }
    const char *pattern = pick.key ? pick.key + (sizeof("font_resolver:") - 1) : NULL;
    pick.cb(url, (void *)pattern, on_fetch, (void *)(intptr_t)e->id);
}

static void on_fetch(const uint8_t *data, size_t len, const char *hint, void *usr) {
    (void)hint;
    font_entry_t *e = find_by_id((int32_t)(intptr_t)usr);
    if (!e) { free((void *)data); return; }

    if (!data) {
        set_error(e, "fetch failed");
        return;
    }

    e->state = GLY_FONT_DECODING;

    const gamely_font_driver_t *drv = find_driver();
    if (!drv || !drv->decode) {
        free((void *)data);
        set_error(e, "no font driver");
        return;
    }

    void *face = drv->decode(data, len);
    free((void *)data);
    if (!face) {
        set_error(e, "decode failed");
        return;
    }
    e->face  = face;
    e->state = GLY_FONT_READY;
}

/* ── public API ───────────────────────────────────────────────────── */

void gamely_daemon_font_start(void *loop) {
    (void)loop;
    memset(s_fonts, 0, sizeof(s_fonts));
    memset(s_families, 0, sizeof(s_families));
    s_next_id = 1;
}

void gamely_daemon_font_stop(void) {
    for (int i = 0; i < FONT_CAP; i++)
        if (s_fonts[i].active) entry_free(&s_fonts[i]);
    memset(s_families, 0, sizeof(s_families));
}

int32_t gamely_daemon_font_get_id(const char *url) {
    if (!url) return -1;
    font_entry_t *e = find_by_url(url);
    if (e) return e->id;
    e = alloc_entry();
    if (!e) { printf("[font] no free slots for '%s'\n", url); return -1; }
    e->id     = s_next_id++;
    e->url    = strdup(url);
    e->state  = GLY_FONT_SEARCHING;
    e->active = 1;
    dispatch_url(e, url);
    return e->id;
}

int32_t gamely_daemon_font_load_memory(const void *data, size_t len) {
    font_entry_t *e = alloc_entry();
    if (!e) { printf("[font] no free slots for memory font\n"); return -1; }
    e->id     = s_next_id++;
    e->url    = NULL;
    e->state  = GLY_FONT_DECODING;
    e->active = 1;

    const gamely_font_driver_t *drv = find_driver();
    if (!drv || !drv->decode) { set_error(e, "no font driver"); return e->id; }

    void *face = drv->decode((const uint8_t *)data, len);
    if (!face) { set_error(e, "decode failed"); return e->id; }
    e->face  = face;
    e->state = GLY_FONT_READY;
    return e->id;
}

gamely_font_state_t gamely_daemon_font_get_state(int32_t id) {
    if (id < 0) return GLY_FONT_ERROR;
    font_entry_t *e = find_by_id(id);
    return e ? e->state : GLY_FONT_ERROR;
}

const char *gamely_daemon_font_get_error(int32_t id) {
    font_entry_t *e = find_by_id(id);
    return e ? e->error_msg : NULL;
}

void gamely_daemon_font_unload_id(int32_t id) {
    font_entry_t *e = find_by_id(id);
    if (!e) return;
    if (e->face) {
        const gamely_font_driver_t *drv = find_driver();
        if (drv && drv->face_free) drv->face_free(e->face);
    }
    entry_free(e);
}

void gamely_daemon_font_unload_url(const char *url) {
    font_entry_t *e = find_by_url(url);
    if (!e) return;
    gamely_daemon_font_unload_id(e->id);
}

void gamely_daemon_font_unload_all(void) {
    const gamely_font_backend_t *b = find_backend();
    if (b && b->unload_all) b->unload_all();
    for (int i = 0; i < FONT_CAP; i++)
        if (s_fonts[i].active) entry_free(&s_fonts[i]);
    memset(s_families, 0, sizeof(s_families));
}

/* ── utf-8 decode (minimal, no validation of overlongs/surrogates) ──── */

static uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    if (s[0] < 0x80) { *p += 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0 && s[1]) { *p += 2; return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); }
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
        *p += 3;
        return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        *p += 4;
        return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *p += 1;
    return s[0];
}

/* ── centralized text layout ─────────────────────────────────────── */

void gamely_daemon_font_draw_text(int32_t font_id, uint8_t px_size,
                                   int16_t x, int16_t y, const char *utf8, uint32_t rgba) {
    font_entry_t *e = find_by_id(font_id);
    if (!e || e->state != GLY_FONT_READY || !utf8) return;
    const gamely_font_driver_t  *drv     = find_driver();
    const gamely_font_backend_t *backend = find_backend();
    if (!drv || !backend) return;

    int16_t ascent = px_size, descent = 0, line_h = px_size;
    if (drv->metrics) drv->metrics(e->face, px_size, &ascent, &descent, &line_h);

    int16_t cursor_x   = x;
    int16_t baseline_y = y + ascent;
    const char *p = utf8;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == '\n') { cursor_x = x; baseline_y += line_h; continue; }
        gamely_font_glyph_t g;
        if (!drv->glyph(e->face, px_size, cp, backend, &g) &&
            !drv->glyph(e->face, px_size, '?', backend, &g)) continue;
        if (g.w > 0 && g.h > 0) {
            float u0 = (float)g.atlas_x / GAMELY_FONT_ATLAS_SIZE;
            float v0 = (float)g.atlas_y / GAMELY_FONT_ATLAS_SIZE;
            float u1 = (float)(g.atlas_x + g.w) / GAMELY_FONT_ATLAS_SIZE;
            float v1 = (float)(g.atlas_y + g.h) / GAMELY_FONT_ATLAS_SIZE;
            backend->draw_quad(cursor_x + g.bearing_x, baseline_y - g.bearing_y,
                                g.w, g.h, u0, v0, u1, v1, rgba);
        }
        cursor_x += g.advance;
    }
}

void gamely_daemon_font_mensure_text(int32_t font_id, uint8_t px_size,
                                      const char *utf8, int16_t *w, int16_t *h) {
    if (w) *w = 0;
    if (h) *h = 0;
    font_entry_t *e = find_by_id(font_id);
    if (!e || e->state != GLY_FONT_READY || !utf8) return;
    const gamely_font_driver_t  *drv     = find_driver();
    const gamely_font_backend_t *backend = find_backend();
    if (!drv || !backend) return;

    int16_t ascent = px_size, descent = 0, line_h = px_size;
    if (drv->metrics) drv->metrics(e->face, px_size, &ascent, &descent, &line_h);

    int16_t cursor_x = 0, max_x = 0;
    int     lines     = 1;
    const char *p = utf8;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == '\n') { if (cursor_x > max_x) max_x = cursor_x; cursor_x = 0; lines++; continue; }
        gamely_font_glyph_t g;
        if (!drv->glyph(e->face, px_size, cp, backend, &g) &&
            !drv->glyph(e->face, px_size, '?', backend, &g)) continue;
        cursor_x += g.advance;
    }
    if (cursor_x > max_x) max_x = cursor_x;
    if (w) *w = max_x;
    if (h) *h = (int16_t)(lines * line_h);
}
