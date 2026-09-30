#include <stdlib.h>
#include <string.h>
#include "gecnd.h"
#include "gdweb.h"

/* ── per-request accumulation context ────────────────────────────── */

typedef struct {
    gamely_fetch_done_cb  on_done;
    void                  *on_done_usr;
    uint8_t               *buf;
    size_t                 len;
    size_t                 cap;
    char                   hint[16];
    int                    failed;
} fetch_ctx_t;

static void on_status(gdweb_id_t id, int status, void *usr) {
    (void)id;
    fetch_ctx_t *ctx = (fetch_ctx_t *)usr;
    if (status < 200 || status >= 300) ctx->failed = 1;
}

static void on_data(gdweb_id_t id, const char *data, size_t len, void *usr) {
    (void)id;
    fetch_ctx_t *ctx = (fetch_ctx_t *)usr;
    if (ctx->failed) return;
    if (ctx->len + len > ctx->cap) {
        size_t    new_cap = ctx->cap ? ctx->cap * 2 : 65536;
        while (new_cap < ctx->len + len) new_cap *= 2;
        uint8_t *nb = realloc(ctx->buf, new_cap);
        if (!nb) { ctx->failed = 1; return; }
        ctx->buf = nb;
        ctx->cap = new_cap;
    }
    memcpy(ctx->buf + ctx->len, data, len);
    ctx->len += len;
}

static void on_done(gdweb_id_t id, void *usr) {
    (void)id;
    fetch_ctx_t *ctx = (fetch_ctx_t *)usr;
    if (ctx->failed || !ctx->buf) {
        free(ctx->buf);
        ctx->on_done(NULL, 0, NULL, ctx->on_done_usr);
    } else {
        ctx->on_done(ctx->buf, ctx->len,
                     ctx->hint[0] ? ctx->hint : NULL,
                     ctx->on_done_usr);
        /* buf ownership transferred to the caller — it will call release(buf) */
    }
    free(ctx);
}

static void on_error(gdweb_id_t id, const char *msg, void *usr) {
    (void)id; (void)msg;
    fetch_ctx_t *ctx = (fetch_ctx_t *)usr;
    free(ctx->buf);
    ctx->on_done(NULL, 0, NULL, ctx->on_done_usr);
    free(ctx);
}

void gamely_web_fetch(const char *url, const char *hint,
                       gamely_fetch_done_cb done_cb, void *done_usr) {
    fetch_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) { done_cb(NULL, 0, NULL, done_usr); return; }

    ctx->on_done     = done_cb;
    ctx->on_done_usr = done_usr;

    if (hint) {
        strncpy(ctx->hint, hint, sizeof(ctx->hint) - 1);
    } else {
        /* derive hint from the file extension of the last path segment */
        gecnd_lang_t  it      = {{ "url", url }};
        const char   *seg     = NULL;
        size_t        seg_len = 0;
        while (gecnd_lang(&it)) {
            if (it.url.kind == GECND_URL_KIND_PATH) {
                seg     = it.url.ptr;
                seg_len = it.url.len;
            }
        }
        if (seg) {
            const char *dot = NULL;
            for (size_t i = 0; i < seg_len; i++)
                if (seg[i] == '.') dot = seg + i;
            if (dot) {
                size_t ext_len = seg_len - (size_t)(dot + 1 - seg);
                if (ext_len && ext_len < sizeof(ctx->hint)) {
                    memcpy(ctx->hint, dot + 1, ext_len);
                    ctx->hint[ext_len] = '\0';
                }
            }
        }
    }

    gdweb_control_client()->http(url, NULL, on_status, on_data, on_done, on_error, ctx);
}
