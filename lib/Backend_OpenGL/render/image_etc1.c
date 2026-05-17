#include "gecnd.h"
#include "geopengl.h"

/* ETC1 is GLES-only; on desktop GL there is no GL_ETC1_RGB8_OES symbol and
 * no driver path, so the entire backend compiles out. */
#if defined(GECND_OPENGLES) && GECND_OPENGLES == 1

#include <stdlib.h>

/* Each ETC1 image owns a dedicated GL_TEXTURE_2D (no shared atlas) — Mali400
 * has known issues with sub-update on compressed textures, and ETC1 in this
 * engine is typically used for large standalone images (e.g. 1920x600) where
 * an atlas wouldn't fit more than a couple anyway. */

static kvec_t(GLTexture*) s_live_etc1;
static bool               s_live_etc1_init = false;

static void ensure_live(void) {
    if (!s_live_etc1_init) { kv_init(s_live_etc1); s_live_etc1_init = true; }
}

static void etc1_upload(int32_t id, void **backend_data,
                        const uint8_t *data, size_t len,
                        int16_t w, int16_t h,
                        gamely_img_release_cb release) {
    (void)id;
    ensure_live();

    /* Padded (block) dimensions; the payload covers this entire area. */
    int pw = (w + 3) & ~3;
    int ph = (h + 3) & ~3;

    size_t expected = (size_t)(pw / 4) * (size_t)(ph / 4) * 8;
    if (len != expected) {
        release((void *)data);
        *backend_data = NULL;
        return;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
                           (GLsizei)pw, (GLsizei)ph, 0,
                           (GLsizei)len, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLTexture *t  = malloc(sizeof(GLTexture));
    t->id         = tex;
    t->width      = (int)w;
    t->height     = (int)h;
    t->atlas_x    = 0;
    t->atlas_y    = 0;
    /* UV crop covers original (non-padded) dimensions only. */
    t->u          = 0.0f;
    t->v          = 0.0f;
    t->u2         = (float)w / (float)pw;
    t->v2         = (float)h / (float)ph;
    t->is_opaque  = true;          /* ETC1 has no alpha */
    /* page_index doubles as batch dispatcher key: the FLAG marks it as a raw
     * GL texture id (not an atlas page index). */
    t->page_index = (int)tex | GECND_ATLAS_ETC_PAGE_FLAG;
    t->live_idx   = (int)kv_size(s_live_etc1);
    kv_push(GLTexture*, s_live_etc1, t);
    *backend_data = t;

    release((void *)data);
}

static void etc1_draw(int32_t id, void *backend_data, int16_t x, int16_t y) {
    (void)id;
    GLBackendState *s = geogl_get_state();
    GLTexture     *t  = (GLTexture *)backend_data;
    if (!t) return;

    static const uint32_t color = 0xFFFFFFFF;
    int16_t iw = (int16_t)t->width;
    int16_t ih = (int16_t)t->height;

    /* Standalone texture: UVs already span the visible region exactly; no
     * half-pixel bleed correction needed across atlas neighbours. */
    float u1 = t->u,  v1 = t->v;
    float u2 = t->u2, v2 = t->v2;

    ge_batch_add_vertex_tex(x,      y,      u1, v1, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x,      y + ih, u1, v2, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x + iw, y + ih, u2, v2, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x,      y,      u1, v1, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x + iw, y + ih, u2, v2, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x + iw, y,      u2, v1, color, t->is_opaque, t->page_index);

    s->current_z++;
}

static void etc1_unload(int32_t id, void *backend_data) {
    (void)id;
    GLTexture *t = (GLTexture *)backend_data;
    if (!t) return;

    GLuint tex = t->id;
    glDeleteTextures(1, &tex);

    int    idx = t->live_idx;
    size_t n   = kv_size(s_live_etc1);
    if (idx >= 0 && (size_t)idx < n) {
        GLTexture *last  = s_live_etc1.a[n - 1];
        s_live_etc1.a[idx] = last;
        last->live_idx   = idx;
        s_live_etc1.n    = n - 1;
    }
    free(t);
}

static void etc1_unload_all(void) {
    for (size_t i = 0; i < kv_size(s_live_etc1); i++) {
        GLuint tex = s_live_etc1.a[i]->id;
        glDeleteTextures(1, &tex);
        free(s_live_etc1.a[i]);
    }
    s_live_etc1.n = 0;
}

const gamely_img_backend_t s_backend_etc1 = {
    .upload     = etc1_upload,
    .draw       = etc1_draw,
    .unload     = etc1_unload,
    .unload_all = etc1_unload_all,
};

#endif /* GECND_OPENGLES == 1 */
