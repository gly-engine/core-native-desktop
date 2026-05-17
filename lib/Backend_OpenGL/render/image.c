#include <stdlib.h>
#include "gecnd.h"
#include "geopengl.h"

static kvec_t(GLTexture*) s_live;
static bool               s_live_init = false;

static void ensure_live(void) {
    if (!s_live_init) { kv_init(s_live); s_live_init = true; }
}

static void gl_upload(int32_t id, void **backend_data,
                      const uint8_t *data, size_t len,
                      int16_t w, int16_t h,
                      gamely_img_release_cb release) {
    (void)id; (void)len;
    ensure_live();
    GLBackendState *s = geogl_get_state();
    int ox, oy, page_idx = 0;
    ge_atlas_alloc((int)w, (int)h, &page_idx, &ox, &oy);

    bool is_opaque = true;
    for (size_t i = 3; i < (size_t)(w * h * 4); i += 4) {
        if (data[i] < 254) {
            is_opaque = false;
            break;
        }
    }

    glBindTexture(GL_TEXTURE_2D, s->atlas_pages.a[page_idx].tex_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, (GLsizei)w, (GLsizei)h,
                    GL_RGBA, GL_UNSIGNED_BYTE, data);
    s->atlas_dirty = true;

    GLTexture *t   = malloc(sizeof(GLTexture));
    t->id          = s->atlas_pages.a[page_idx].tex_id;
    t->width       = (int)w;
    t->height      = (int)h;
    t->atlas_x     = ox;
    t->atlas_y     = oy;
    t->u           = (float)ox / (float)GE_ATLAS_SIZE;
    t->v           = (float)oy / (float)GE_ATLAS_SIZE;
    t->u2          = (float)(ox + w) / (float)GE_ATLAS_SIZE;
    t->v2          = (float)(oy + h) / (float)GE_ATLAS_SIZE;
    t->is_opaque   = is_opaque;
    t->page_index  = page_idx;
    t->live_idx    = (int)kv_size(s_live);
    kv_push(GLTexture*, s_live, t);
    *backend_data  = t;

    release((void *)data);
}

static void gl_draw(int32_t id, void *backend_data, int16_t x, int16_t y) {
    (void)id;
    GLBackendState *s  = geogl_get_state();
    GLTexture      *t  = (GLTexture *)backend_data;
    if (!t) return;

    static const uint32_t color = 0xFFFFFFFF;
    int16_t iw = (int16_t)t->width;
    int16_t ih = (int16_t)t->height;

    float half = 0.5f / (float)GE_ATLAS_SIZE;
    float u1 = t->u  + half, v1 = t->v  + half;
    float u2 = t->u2 - half, v2 = t->v2 - half;

    ge_batch_add_vertex_tex(x,      y,      u1, v1, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x,      y + ih, u1, v2, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x + iw, y + ih, u2, v2, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x,      y,      u1, v1, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x + iw, y + ih, u2, v2, color, t->is_opaque, t->page_index);
    ge_batch_add_vertex_tex(x + iw, y,      u2, v1, color, t->is_opaque, t->page_index);

    s->current_z++;
}

static void gl_unload(int32_t id, void *backend_data) {
    (void)id;
    GLTexture *t = (GLTexture *)backend_data;
    if (!t) return;

    ge_atlas_free(t->page_index, t->atlas_x, t->atlas_y, t->width, t->height);

    /* swap-pop from live list */
    int    idx = t->live_idx;
    size_t n   = kv_size(s_live);
    if (idx >= 0 && (size_t)idx < n) {
        GLTexture *last = s_live.a[n - 1];
        s_live.a[idx]   = last;
        last->live_idx  = idx;
        s_live.n        = n - 1;
    }
    free(t);
}

static void gl_unload_all(void) {
    for (size_t i = 0; i < kv_size(s_live); i++) {
        free(s_live.a[i]);
    }
    s_live.n = 0;
    ge_atlas_reset_images();
}

static const gamely_img_backend_t s_backend = {
    .upload     = gl_upload,
    .draw       = gl_draw,
    .unload     = gl_unload,
    .unload_all = gl_unload_all,
};

#if defined(GECND_OPENGLES) && GECND_OPENGLES == 1
extern const gamely_img_backend_t s_backend_etc1;
#endif

void gamely_daemon_img_opengl_register(void) {
#if defined(GECND_OPENGLES) && GECND_OPENGLES == 1
    if (geogl_get_state()->etc1_supported) {
        gamely_daemon_img_register_backend("etc1", &s_backend_etc1);
    }
#endif
    gamely_daemon_img_register_backend("rgba", &s_backend);
}
