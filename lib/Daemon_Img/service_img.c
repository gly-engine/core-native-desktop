#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "gecnd.h"

#define IMG_CAP     256

/* ── image entry ──────────────────────────────────────────────────── */

typedef struct {
    int32_t            id;
    char              *url;
    gamely_img_state_t state;
    char               fmt[16];
    int16_t            w, h;
    void              *backend_data;
    char              *error_msg;
    void              *src_owned;
    int                active;
} img_entry_t;

static img_entry_t s_imgs[IMG_CAP];
static int32_t     s_next_id = 1;

/* ── registries ───────────────────────────────────────────────────── */

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
    free(e->src_owned);
    memset(e, 0, sizeof(*e));
}

/* ── registry helpers ─────────────────────────────────────────────── */

static gamely_img_decoder_cb find_decoder(const char *from, const char *to, bool *threaded) {
    char  key[96];
    void *cb = NULL;
    snprintf(key, sizeof(key), "image_decoder_async:%s:%s", from, to);
    if (gecnd_registry("get", key, (void *)&cb, NULL)) { if (threaded) *threaded = true;  return (gamely_img_decoder_cb)cb; }
    snprintf(key, sizeof(key), "image_decoder_sync:%s:%s", from, to);
    if (gecnd_registry("get", key, (void *)&cb, NULL)) { if (threaded) *threaded = false; return (gamely_img_decoder_cb)cb; }
    return NULL;
}

static const gamely_img_backend_t *find_backend(const char *fmt) {
    char  key[64];
    void *cbs = NULL;
    snprintf(key, sizeof(key), "image_backend:%s", fmt);
    gecnd_registry("get", key, (void *)&cbs, NULL);
    return (const gamely_img_backend_t *)cbs;
}

typedef struct {
    const char                 *from;
    gamely_img_decoder_cb       cb;
    bool                        threaded;
    char                        to[16];
    const gamely_img_backend_t *backend;
} backend_pick_t;

static void backend_pick(const char *key, void *value, void *usr) {
    backend_pick_t *p = (backend_pick_t *)usr;
    if (p->cb) return;
    const char *fmt = key + (sizeof("image_backend:") - 1);
    bool threaded = false;
    gamely_img_decoder_cb cb = find_decoder(p->from, fmt, &threaded);
    if (cb) {
        p->cb       = cb;
        p->threaded = threaded;
        p->backend  = (const gamely_img_backend_t *)value;
        strncpy(p->to, fmt, sizeof(p->to) - 1);
        p->to[sizeof(p->to) - 1] = '\0';
    }
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
static void release_noop(void *ptr) { (void)ptr; }

/* ── threaded decode via uv thread pool ───────────────────────────── */

typedef struct {
    uv_work_t             work;
    int32_t               entry_id;
    gamely_img_decoder_cb cb;
    uint8_t              *src;
    size_t                src_len;
    gamely_img_decoded_t  result;
} decode_work_t;

static void decode_work_cb(uv_work_t *req) {
    decode_work_t *w = (decode_work_t *)req;
    w->result = w->cb(w->src, w->src_len);
    if (!(w->result.flags & GECND_FLAG_IMG_MOVE)) {
        free(w->src);
        w->src = NULL;
    }
}

static void decode_after_cb(uv_work_t *req, int status) {
    decode_work_t *w = (decode_work_t *)req;
    (void)status;
    img_entry_t *e = find_by_id(w->entry_id);
    bool move = (w->result.flags & GECND_FLAG_IMG_MOVE) != 0;
    if (!e || !w->result.pixels) {
        if (e) fprintf(stderr, "[img] decode failed id=%d '%s' (%zu bytes)\n",
                       e->id, e->url, w->src_len);
        if (move) free(w->src); else free(w->result.pixels);
        free(w);
        if (e) set_error(e, "decode failed");
        return;
    }
    const gamely_img_backend_t *b = find_backend(e->fmt);
    if (b) {
        e->w = w->result.w;
        e->h = w->result.h;
        if (move) e->src_owned = w->src;
        b->upload(e->id, &e->backend_data,
                  w->result.pixels, w->result.len, e->w, e->h,
                  w->result.color_format, move ? release_noop : release_free);
        set_ready(e);
    } else {
        if (move) free(w->src); else free(w->result.pixels);
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

    const char *from = hint ? hint : "";

    backend_pick_t pick = {0};
    pick.from = from;
    gecnd_registry("get", "image_backend:*", backend_pick, &pick);
    if (!pick.cb) {
        printf("[img] no decoder '%s'→backend for '%s'\n", from, e->url);
        free((void *)data);
        set_error(e, "no decoder for format");
        return;
    }

    strncpy(e->fmt, pick.to, sizeof(e->fmt) - 1);
    e->state = GLY_IMG_DECODING;

    if (pick.threaded && s_loop) {
        decode_work_t *w = calloc(1, sizeof(*w));
        if (!w) { free((void *)data); set_error(e, "oom"); return; }
        w->entry_id = e->id;
        w->cb       = pick.cb;
        w->src      = (uint8_t *)data;
        w->src_len  = len;
        uv_queue_work(s_loop, &w->work, decode_work_cb, decode_after_cb);
        return;
    }

    gamely_img_decoded_t result = pick.cb(data, len);
    if (!result.pixels) {
        free((void *)data);
        fprintf(stderr, "[img] decode failed id=%d '%s' (->%s, %zu bytes)\n",
                e->id, e->url, pick.to, len);
        set_error(e, "decode failed");
        return;
    }
    bool move = (result.flags & GECND_FLAG_IMG_MOVE) != 0;
    if (move) e->src_owned = (void *)data;
    else      free((void *)data);
    e->w = result.w;
    e->h = result.h;
    pick.backend->upload(e->id, &e->backend_data,
                         result.pixels, result.len, e->w, e->h,
                         result.color_format, move ? release_noop : release_free);
    set_ready(e);
}

typedef struct { gamely_img_schema_cb cb; const char *key; } resolver_pick_t;

static void resolver_pick(const char *key, void *value, void *usr) {
    resolver_pick_t *p = (resolver_pick_t *)usr;
    p->cb  = (gamely_img_schema_cb)value;
    p->key = key;
}

static void dispatch_url(img_entry_t *e, const char *url) {
    resolver_pick_t pick = { NULL, NULL };
    char *key = malloc(strlen(url) + sizeof("image_resolver:()"));
    if (key) {
        sprintf(key, "image_resolver:(%s)", url);
        gecnd_registry("get", key, (void *)resolver_pick, &pick);
        free(key);
    }
    if (!pick.cb) {
        printf("[img] no resolver for '%s'\n", url);
        set_error(e, "no schema handler");
        return;
    }
    const char *pattern = pick.key ? pick.key + (sizeof("image_resolver:") - 1) : NULL;
    pick.cb(url, (void *)pattern, on_fetch, (void *)(intptr_t)e->id);
}

/* ── public API ───────────────────────────────────────────────────── */

void gamely_daemon_img_start(void *loop) {
    s_loop = (uv_loop_t *)loop;
    memset(s_imgs, 0, sizeof(s_imgs));
    s_next_id  = 1;
}

void gamely_daemon_img_stop(void) {
    for (int i = 0; i < IMG_CAP; i++)
        if (s_imgs[i].active) entry_free(&s_imgs[i]);
    s_loop = NULL;
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
    if (id < 0) return GLY_IMG_ERROR;
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
    const gamely_img_backend_t *b = find_backend(e->fmt);
    if (b) b->draw(id, e->backend_data, x, y);
}

void gamely_daemon_img_unload_id(int32_t id) {
    img_entry_t *e = find_by_id(id);
    if (!e) return;
    const gamely_img_backend_t *b = find_backend(e->fmt);
    if (b && e->state == GLY_IMG_READY) b->unload(id, e->backend_data);
    entry_free(e);
}

void gamely_daemon_img_unload_url(const char *url) {
    img_entry_t *e = find_by_url(url);
    if (!e) return;
    gamely_daemon_img_unload_id(e->id);
}

static void backend_unload_all(const char *key, void *value, void *usr) {
    (void)key; (void)usr;
    const gamely_img_backend_t *b = (const gamely_img_backend_t *)value;
    if (b->unload_all) b->unload_all();
}

void gamely_daemon_img_unload_all(void) {
    gecnd_registry("get", "image_backend:*", backend_unload_all, NULL);
    for (int i = 0; i < IMG_CAP; i++)
        if (s_imgs[i].active) entry_free(&s_imgs[i]);
}

bool gamely_daemon_img_has_backend(const char *fmt) {
    return fmt && find_backend(fmt) != NULL;
}

bool gamely_daemon_img_can_decode(const char *from) {
    if (!from) return false;
    backend_pick_t pick = {0};
    pick.from = from;
    gecnd_registry("get", "image_backend:*", backend_pick, &pick);
    return pick.cb != NULL;
}

int32_t gamely_daemon_img_loading_count(void) {
    int32_t n = 0;
    for (int i = 0; i < IMG_CAP; i++)
        if (s_imgs[i].active &&
            (s_imgs[i].state == GLY_IMG_SEARCHING ||
             s_imgs[i].state == GLY_IMG_DECODING))
            n++;
    return n;
}
