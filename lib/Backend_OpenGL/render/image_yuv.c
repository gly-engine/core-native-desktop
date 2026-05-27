#include <stdlib.h>
#include "gecnd.h"
#include "geopengl.h"

/* YUV420P image backend. Consumes the tightly-packed I420 blob produced by the
 * jpeg->yuv decoder ([Y w*h][Cb w/2*h/2][Cr w/2*h/2]) and uploads the three
 * planes into the YUV atlas (Y full-res, Cb/Cr half-res, shared normalized UV).
 * YCbCr->RGB happens in the fragment shader, so nothing is converted on the CPU. */

typedef struct {
    int   page_index;     /* index into yuv_atlas_pages */
    int   width, height;  /* Y-space (full-res) pixels */
    int   atlas_x, atlas_y;
    float u, v, u2, v2;   /* normalized, shared by all three planes */
    int   live_idx;
} GLYuvTexture;

static kvec_t(GLYuvTexture*) s_live;
static bool                  s_live_init = false;

static void ensure_live(void) {
    if (!s_live_init) { kv_init(s_live); s_live_init = true; }
}

static void yuv_upload(int32_t id, void **backend_data,
                       const uint8_t *data, size_t len,
                       int16_t w, int16_t h,
                       gamely_img_release_cb release) {
    (void)id; (void)len;
    ensure_live();
    GLBackendState *s = geogl_get_state();

    int ww = (int)w, hh = (int)h;
    int cw = ww / 2, ch = hh / 2;

    int page_idx = 0, ox = 0, oy = 0;
    ge_atlas_yuv_alloc(ww, hh, &page_idx, &ox, &oy);
    GEYuvAtlasPage *p = &s->yuv_atlas_pages.a[page_idx];

    const uint8_t *plane_y = data;
    const uint8_t *plane_u = data + (size_t)ww * hh;
    const uint8_t *plane_v = plane_u + (size_t)cw * ch;

    /* Planes are tightly packed; widths aren't 4-aligned in general. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, p->tex_y);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, ww, hh, GL_ALPHA, GL_UNSIGNED_BYTE, plane_y);
    glBindTexture(GL_TEXTURE_2D, p->tex_u);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ox / 2, oy / 2, cw, ch, GL_ALPHA, GL_UNSIGNED_BYTE, plane_u);
    glBindTexture(GL_TEXTURE_2D, p->tex_v);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ox / 2, oy / 2, cw, ch, GL_ALPHA, GL_UNSIGNED_BYTE, plane_v);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    GLYuvTexture *t = malloc(sizeof(GLYuvTexture));
    t->page_index = page_idx;
    t->width      = ww;
    t->height     = hh;
    t->atlas_x    = ox;
    t->atlas_y    = oy;
    t->u          = (float)ox / (float)GE_YUV_ATLAS_SIZE;
    t->v          = (float)oy / (float)GE_YUV_ATLAS_SIZE;
    t->u2         = (float)(ox + ww) / (float)GE_YUV_ATLAS_SIZE;
    t->v2         = (float)(oy + hh) / (float)GE_YUV_ATLAS_SIZE;
    t->live_idx   = (int)kv_size(s_live);
    kv_push(GLYuvTexture*, s_live, t);
    *backend_data = t;

    release((void *)data);
}

static void yuv_draw(int32_t id, void *backend_data, int16_t x, int16_t y) {
    (void)id;
    GLBackendState *s = geogl_get_state();
    GLYuvTexture   *t = (GLYuvTexture *)backend_data;
    if (!t) return;

    static const uint32_t color = 0xFFFFFFFF;
    int16_t iw = (int16_t)t->width;
    int16_t ih = (int16_t)t->height;

    float half = 0.5f / (float)GE_YUV_ATLAS_SIZE;
    float u1 = t->u  + half, v1 = t->v  + half;
    float u2 = t->u2 - half, v2 = t->v2 - half;

    ge_batch_add_vertex_yuv(x,      y,      u1, v1, color, t->page_index);
    ge_batch_add_vertex_yuv(x,      y + ih, u1, v2, color, t->page_index);
    ge_batch_add_vertex_yuv(x + iw, y + ih, u2, v2, color, t->page_index);
    ge_batch_add_vertex_yuv(x,      y,      u1, v1, color, t->page_index);
    ge_batch_add_vertex_yuv(x + iw, y + ih, u2, v2, color, t->page_index);
    ge_batch_add_vertex_yuv(x + iw, y,      u2, v1, color, t->page_index);

    s->current_z++;
}

static void yuv_unload(int32_t id, void *backend_data) {
    (void)id;
    GLYuvTexture *t = (GLYuvTexture *)backend_data;
    if (!t) return;

    ge_atlas_yuv_free(t->page_index, t->atlas_x, t->atlas_y, t->width, t->height);

    /* swap-pop from live list */
    int    idx = t->live_idx;
    size_t n   = kv_size(s_live);
    if (idx >= 0 && (size_t)idx < n) {
        GLYuvTexture *last = s_live.a[n - 1];
        s_live.a[idx]      = last;
        last->live_idx     = idx;
        s_live.n           = n - 1;
    }
    free(t);
}

static void yuv_unload_all(void) {
    for (size_t i = 0; i < kv_size(s_live); i++) {
        free(s_live.a[i]);
    }
    s_live.n = 0;
    ge_atlas_yuv_reset();
}

const gamely_img_backend_t s_backend_yuv = {
    .upload     = yuv_upload,
    .draw       = yuv_draw,
    .unload     = yuv_unload,
    .unload_all = yuv_unload_all,
};
