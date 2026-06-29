#include <stdlib.h>
#include "gecnd.h"
#include "geopengl.h"

/* Generic image backend, driven by the decoded color format:
 *   - rgba8888 / rgba5551 : single-texture atlas page (shelf-allocated).
 *   - yuv420p             : YUV atlas triplet (Y full, Cb/Cr half); YCbCr->RGB
 *                           in the fragment shader.
 *   - etc1                : standalone compressed texture (GLES), bound by raw id.
 * The format is sampled by GE_PROG_ATLAS (single) or GE_PROG_ATLAS_YUV. */

static kvec_t(GLTexture*) s_live;
static bool               s_live_init = false;

static void ensure_live(void) {
    if (!s_live_init) { kv_init(s_live); s_live_init = true; }
}

/* Per-format opacity scan (1-bit/8-bit alpha). */
static bool scan_opaque(const uint8_t *data, int w, int h, GECNDColorFormat fmt) {
    size_t n = (size_t)w * h;
    if (fmt == GECND_PIX_FMT_RGBA8888) {
        for (size_t i = 0; i < n; i++)
            if (data[i * 4 + 3] < 254) return false;
        return true;
    }
    if (fmt == GECND_PIX_FMT_RGBA5551) {
        const uint16_t *px = (const uint16_t *)data;
        for (size_t i = 0; i < n; i++)
            if ((px[i] & 1) == 0) return false;
        return true;
    }
    return true;  /* yuv420p / etc1: no alpha */
}

static void gl_upload(int32_t id, void **backend_data,
                      const uint8_t *data, size_t len,
                      int16_t w, int16_t h,
                      GECNDColorFormat color_format,
                      gamely_img_release_cb release) {
    (void)id; (void)len;   /* len only used by the ETC1 (GLES) path */
    ensure_live();
    GLBackendState *s = geogl_get_state();

    int ww = (int)w, hh = (int)h;
    bool is_opaque = scan_opaque(data, ww, hh, color_format);

    GLTexture *t = malloc(sizeof(GLTexture));
    t->width = ww; t->height = hh; t->is_opaque = is_opaque;
    t->color_format = color_format;

#if defined(GECND_OPENGLES) && GECND_OPENGLES == 1
    if (color_format == GECND_PIX_FMT_ETC1) {
        int pw = (ww + 3) & ~3, ph = (hh + 3) & ~3;
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
                               (GLsizei)pw, (GLsizei)ph, 0, (GLsizei)len, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ge_atlas_etc1_add(tex);
        t->id         = tex;
        t->atlas_x    = 0; t->atlas_y = 0;
        t->u  = 0.0f; t->v  = 0.0f;
        t->u2 = (float)ww / (float)pw; t->v2 = (float)hh / (float)ph;
        t->page_index = (int)tex | GECND_ATLAS_ETC_PAGE_FLAG;
        goto done;
    }
#endif
    {
        int page_idx = 0, ox = 0, oy = 0;
        ge_atlas_acquire(color_format, is_opaque, ww, hh, &page_idx, &ox, &oy);
        GEAtlasPage *p = &s->atlas_pages.a[page_idx];

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (color_format == GECND_PIX_FMT_YUV420P) {
            int cw = ww / 2, ch = hh / 2;
            const uint8_t *py = data;
            const uint8_t *pu = data + (size_t)ww * hh;
            const uint8_t *pv = pu + (size_t)cw * ch;
            glBindTexture(GL_TEXTURE_2D, p->tex_y);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, ww, hh, GL_ALPHA, GL_UNSIGNED_BYTE, py);
            glBindTexture(GL_TEXTURE_2D, p->tex_u);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ox / 2, oy / 2, cw, ch, GL_ALPHA, GL_UNSIGNED_BYTE, pu);
            glBindTexture(GL_TEXTURE_2D, p->tex_v);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ox / 2, oy / 2, cw, ch, GL_ALPHA, GL_UNSIGNED_BYTE, pv);
        } else {
            GLenum fmt  = GL_RGBA;
            GLenum type = (color_format == GECND_PIX_FMT_RGBA5551)
                        ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_BYTE;
            glBindTexture(GL_TEXTURE_2D, p->tex_id);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, ww, hh, fmt, type, data);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        t->id         = (color_format == GECND_PIX_FMT_YUV420P) ? 0 : p->tex_id;
        t->atlas_x    = ox; t->atlas_y = oy;
        t->u  = (float)ox / (float)GE_ATLAS_SIZE;
        t->v  = (float)oy / (float)GE_ATLAS_SIZE;
        t->u2 = (float)(ox + ww) / (float)GE_ATLAS_SIZE;
        t->v2 = (float)(oy + hh) / (float)GE_ATLAS_SIZE;
        t->page_index = page_idx;
    }

#if defined(GECND_OPENGLES) && GECND_OPENGLES == 1
done:;
#endif
    t->live_idx = (int)kv_size(s_live);
    kv_push(GLTexture*, s_live, t);
    *backend_data = t;
    release((void *)data);
}

static void gl_draw(int32_t id, void *backend_data, int16_t x, int16_t y) {
    (void)id;
    GLBackendState *s = geogl_get_state();
    GLTexture      *t = (GLTexture *)backend_data;
    if (!t) return;

    static const uint32_t color = 0xFFFFFFFF;
    int16_t iw = (int16_t)t->width;
    int16_t ih = (int16_t)t->height;

    /* No bleed correction for standalone ETC1 (UV already spans the image). */
    float half = (t->color_format == GECND_PIX_FMT_ETC1) ? 0.0f : 0.5f / (float)GE_ATLAS_SIZE;
    float u1 = t->u  + half, v1 = t->v  + half;
    float u2 = t->u2 - half, v2 = t->v2 - half;

    if (t->color_format == GECND_PIX_FMT_YUV420P) {
        ge_batch_add_vertex_yuv(x,      y,      u1, v1, color, t->page_index);
        ge_batch_add_vertex_yuv(x,      y + ih, u1, v2, color, t->page_index);
        ge_batch_add_vertex_yuv(x + iw, y + ih, u2, v2, color, t->page_index);
        ge_batch_add_vertex_yuv(x,      y,      u1, v1, color, t->page_index);
        ge_batch_add_vertex_yuv(x + iw, y + ih, u2, v2, color, t->page_index);
        ge_batch_add_vertex_yuv(x + iw, y,      u2, v1, color, t->page_index);
    } else {
        ge_batch_add_vertex_tex(x,      y,      u1, v1, color, t->is_opaque, t->page_index);
        ge_batch_add_vertex_tex(x,      y + ih, u1, v2, color, t->is_opaque, t->page_index);
        ge_batch_add_vertex_tex(x + iw, y + ih, u2, v2, color, t->is_opaque, t->page_index);
        ge_batch_add_vertex_tex(x,      y,      u1, v1, color, t->is_opaque, t->page_index);
        ge_batch_add_vertex_tex(x + iw, y + ih, u2, v2, color, t->is_opaque, t->page_index);
        ge_batch_add_vertex_tex(x + iw, y,      u2, v1, color, t->is_opaque, t->page_index);
    }
}

static void gl_unload(int32_t id, void *backend_data) {
    (void)id;
    GLTexture *t = (GLTexture *)backend_data;
    if (!t) return;

    if (t->color_format == GECND_PIX_FMT_ETC1)
        ge_atlas_etc1_remove(t->id);
    else
        ge_atlas_release(t->page_index, t->atlas_x, t->atlas_y, t->width, t->height);

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
    for (size_t i = 0; i < kv_size(s_live); i++) free(s_live.a[i]);
    s_live.n = 0;
    ge_atlas_etc1_clear();
    ge_atlas_reset_images();
}

static const gamely_img_backend_t s_backend = {
    .upload     = gl_upload,
    .draw       = gl_draw,
    .unload     = gl_unload,
    .unload_all = gl_unload_all,
};

void gamely_daemon_img_opengl_register(void) {
    /* One generic backend registered under each color format the decoders may
     * target (the decoded result carries the actual format). */
    gecnd_registry("set", "image_backend:rgba8888", (void *)&s_backend, NULL);
    gecnd_registry("set", "image_backend:rgba5551", (void *)&s_backend, NULL);
    gecnd_registry("set", "image_backend:yuv420",   (void *)&s_backend, NULL);
#if defined(GECND_OPENGLES) && GECND_OPENGLES == 1
    if (geogl_get_state()->etc1_supported)
        gecnd_registry("set", "image_backend:etc1", (void *)&s_backend, NULL);
#endif
}
