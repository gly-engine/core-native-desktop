#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "gecnd.h"

#define IMG_CAP     256
#define SCHEMA_CAP  16
#define DECODER_CAP 32
#define BACKEND_CAP 16

/* ── image entry ──────────────────────────────────────────────────── */

typedef struct {
    int32_t            id;
    char              *url;
    gamely_img_state_t state;
    char               fmt[16];
    int16_t            w, h;
    void              *backend_data;
    char              *error_msg;
    int                active;
} img_entry_t;

static img_entry_t s_imgs[IMG_CAP];
static int32_t     s_next_id = 1;

/* ── registries ───────────────────────────────────────────────────── */

typedef struct { char prefix[32]; gamely_img_schema_cb  cb; void *usr; } schema_t;
typedef struct { char from[16]; char to[16]; bool use_thread; gamely_img_decoder_cb cb; } decoder_t;
typedef struct { char fmt[16]; gamely_img_backend_t cbs; } backend_t;

static schema_t  s_schemas [SCHEMA_CAP];  static int s_schema_n  = 0;
static decoder_t s_decoders[DECODER_CAP]; static int s_decoder_n = 0;
static backend_t s_backends[BACKEND_CAP]; static int s_backend_n = 0;
static uv_loop_t *s_loop = NULL;

/* ── entry helpers ────────────────────────────────────────────────── */

static img_entry_t *find_by_url(const char *url) {
    for (int i = 0; i < IMG_CAP; i++)
        if (s_imgs[i].active && strcmp(s_imgs[i].url, url) == 0)
            return &s_imgs[i];
    return NULL;
}

static img_entry_t *find_by_id(int32_t id) {
    for (int i = 0; i < IMG_CAP; i++)
        if (s_imgs[i].active && s_imgs[i].id == id)
            return &s_imgs[i];
    return NULL;
}

static img_entry_t *alloc_entry(void) {
    for (int i = 0; i < IMG_CAP; i++)
        if (!s_imgs[i].active) return &s_imgs[i];
    return NULL;
}

static void entry_free(img_entry_t *e) {
    free(e->url);
    free(e->error_msg);
    memset(e, 0, sizeof(*e));
}

/* ── registry helpers ─────────────────────────────────────────────── */

static schema_t *find_schema(const char *url) {
    schema_t *best     = NULL;
    size_t    best_len = 0;
    for (int i = 0; i < s_schema_n; i++) {
        size_t l = strlen(s_schemas[i].prefix);
        if (l >= best_len && strncmp(url, s_schemas[i].prefix, l) == 0) {
            best     = &s_schemas[i];
            best_len = l;
        }
    }
    return best;
}

static decoder_t *find_decoder(const char *from, const char *to) {
    decoder_t *best = NULL;
    for (int i = 0; i < s_decoder_n; i++)
        if (strcmp(s_decoders[i].from, from) == 0 &&
            strcmp(s_decoders[i].to,   to  ) == 0)
            best = &s_decoders[i];
    return best;
}

static backend_t *best_backend(void) {
    return s_backend_n > 0 ? &s_backends[s_backend_n - 1] : NULL;
}

static backend_t *find_backend(const char *fmt) {
    for (int i = s_backend_n - 1; i >= 0; i--)
        if (strcmp(s_backends[i].fmt, fmt) == 0)
            return &s_backends[i];
    return NULL;
}

/* ── state transitions ────────────────────────────────────────────── */

static void set_error(img_entry_t *e, const char *msg) {
    free(e->error_msg);
    e->error_msg = msg ? strdup(msg) : NULL;
    e->state     = GLY_IMG_ERROR;
    printf("[img] error id=%d '%s': %s\n", e->id, e->url, msg ? msg : "");
}

static void set_ready(img_entry_t *e) { e->state = GLY_IMG_READY; }

static void release_free(void *ptr) { free(ptr); }

/* ── threaded decode via uv thread pool ───────────────────────────── */

typedef struct {
    uv_work_t            work;
    int32_t              entry_id;
    decoder_t           *dec;
    uint8_t             *src;
    size_t               src_len;
    gamely_img_decoded_t result;
} decode_work_t;

static void decode_work_cb(uv_work_t *req) {
    decode_work_t *w = (decode_work_t *)req;
    w->result = w->dec->cb(w->src, w->src_len);
    free(w->src);
    w->src = NULL;
}

static void decode_after_cb(uv_work_t *req, int status) {
    decode_work_t *w = (decode_work_t *)req;
    (void)status;
    img_entry_t *e = find_by_id(w->entry_id);
    if (!e || !w->result.pixels) {
        free(w->result.pixels);
        free(w);
        if (e) set_error(e, "decode failed");
        return;
    }
    backend_t *b = find_backend(e->fmt);
    if (b) {
        e->w = w->result.w;
        e->h = w->result.h;
        b->cbs.upload(e->id, &e->backend_data,
                      w->result.pixels, 0, e->w, e->h, release_free);
        set_ready(e);
    } else {
        free(w->result.pixels);
        set_error(e, "no backend");
    }
    free(w);
}

/* ── fetch callback & dispatch ────────────────────────────────────── */

static void dispatch_url(img_entry_t *e, const char *url);

static void on_fetch(const uint8_t *data, size_t len,
                      const char *hint, void *usr) {
    img_entry_t *e = find_by_id((int32_t)(intptr_t)usr);
    if (!e) { free((void *)data); return; }

    if (!data) {
        if (hint) dispatch_url(e, hint);
        else      set_error(e, "fetch failed");
        return;
    }

    backend_t *backend = best_backend();
    if (!backend) {
        free((void *)data);
        set_error(e, "no backend registered");
        return;
    }

    const char *from = hint ? hint : "";
    const char *to   = backend->fmt;
    strncpy(e->fmt, to, sizeof(e->fmt) - 1);
    e->state = GLY_IMG_DECODING;

    if (strcmp(from, to) == 0) {
        backend->cbs.upload(e->id, &e->backend_data,
                            data, len, 0, 0, release_free);
        set_ready(e);
        return;
    }

    decoder_t *dec = find_decoder(from, to);
    if (!dec) {
        printf("[img] no decoder '%s'→'%s' for '%s'\n", from, to, e->url);
        free((void *)data);
        set_error(e, "no decoder for format");
        return;
    }

    if (dec->use_thread && s_loop) {
        decode_work_t *w = calloc(1, sizeof(*w));
        if (!w) { free((void *)data); set_error(e, "oom"); return; }
        w->entry_id = e->id;
        w->dec      = dec;
        w->src     = (uint8_t *)data;
        w->src_len = len;
        uv_queue_work(s_loop, &w->work, decode_work_cb, decode_after_cb);
        return;
    }

    gamely_img_decoded_t result = dec->cb(data, len);
    free((void *)data);
    if (!result.pixels) { set_error(e, "decode failed"); return; }
    e->w = result.w;
    e->h = result.h;
    backend->cbs.upload(e->id, &e->backend_data,
                        result.pixels, 0, e->w, e->h, release_free);
    set_ready(e);
}

static void dispatch_url(img_entry_t *e, const char *url) {
    schema_t *schema = find_schema(url);
    if (!schema) {
        printf("[img] no schema for '%s'\n", url);
        set_error(e, "no schema handler");
        return;
    }
    schema->cb(url, schema->usr, on_fetch, (void *)(intptr_t)e->id);
}

/* ── public API ───────────────────────────────────────────────────── */

void gamely_daemon_img_start(void *loop) {
    s_loop = (uv_loop_t *)loop;
    memset(s_imgs, 0, sizeof(s_imgs));
    s_next_id  = 1;
    s_schema_n = s_decoder_n = s_backend_n = 0;
}

void gamely_daemon_img_stop(void) {
    for (int i = 0; i < IMG_CAP; i++)
        if (s_imgs[i].active) entry_free(&s_imgs[i]);
    s_loop = NULL;
}

void gamely_daemon_img_register_schema(const char *prefix,
                                        gamely_img_schema_cb cb, void *usr) {
    if (s_schema_n >= SCHEMA_CAP) return;
    schema_t *s = &s_schemas[s_schema_n++];
    strncpy(s->prefix, prefix, sizeof(s->prefix) - 1);
    s->cb  = cb;
    s->usr = usr;
}

void gamely_daemon_img_register_decoder(const char *from, const char *to,
                                         bool use_thread,
                                         gamely_img_decoder_cb cb) {
    if (s_decoder_n >= DECODER_CAP) return;
    decoder_t *d = &s_decoders[s_decoder_n++];
    strncpy(d->from, from, sizeof(d->from) - 1);
    strncpy(d->to,   to,   sizeof(d->to)   - 1);
    d->use_thread = use_thread;
    d->cb         = cb;
}

void gamely_daemon_img_register_backend(const char *fmt,
                                         const gamely_img_backend_t *cbs) {
    if (s_backend_n >= BACKEND_CAP) return;
    backend_t *b = &s_backends[s_backend_n++];
    strncpy(b->fmt, fmt, sizeof(b->fmt) - 1);
    b->cbs = *cbs;
}

int32_t gamely_daemon_img_get_id(const char *url) {
    if (!url) return -1;
    img_entry_t *e = find_by_url(url);
    if (e) return e->id;
    e = alloc_entry();
    if (!e) { printf("[img] no free slots for '%s'\n", url); return -1; }
    e->id     = s_next_id++;
    e->url    = strdup(url);
    e->state  = GLY_IMG_SEARCHING;
    e->active = 1;
    dispatch_url(e, url);
    return e->id;
}

gamely_img_state_t gamely_daemon_img_get_state(int32_t id) {
    img_entry_t *e = find_by_id(id);
    return e ? e->state : GLY_IMG_ERROR;
}

const char *gamely_daemon_img_get_error(int32_t id) {
    img_entry_t *e = find_by_id(id);
    return e ? e->error_msg : NULL;
}

void gamely_daemon_img_get_mensure(int32_t id, int16_t *w, int16_t *h) {
    img_entry_t *e = find_by_id(id);
    if (w) *w = e ? e->w : 0;
    if (h) *h = e ? e->h : 0;
}

void gamely_daemon_img_draw(int32_t id, int16_t x, int16_t y) {
    img_entry_t *e = find_by_id(id);
    if (!e || e->state != GLY_IMG_READY) return;
    backend_t *b = find_backend(e->fmt);
    if (b) b->cbs.draw(id, e->backend_data, x, y);
}

void gamely_daemon_img_unload_id(int32_t id) {
    img_entry_t *e = find_by_id(id);
    if (!e) return;
    backend_t *b = find_backend(e->fmt);
    if (b && e->state == GLY_IMG_READY) b->cbs.unload(id, e->backend_data);
    entry_free(e);
}

void gamely_daemon_img_unload_url(const char *url) {
    img_entry_t *e = find_by_url(url);
    if (!e) return;
    gamely_daemon_img_unload_id(e->id);
}

void gamely_daemon_img_unload_all(void) {
    for (int i = s_backend_n - 1; i >= 0; i--)
        if (s_backends[i].cbs.unload_all) s_backends[i].cbs.unload_all();
    for (int i = 0; i < IMG_CAP; i++)
        if (s_imgs[i].active) entry_free(&s_imgs[i]);
}
