#include <stdlib.h>
#include <string.h>
#include "gecnd.h"
#include "../Common_Utils/uri.h"

/* ── per-request accumulation context ────────────────────────────── */

typedef struct {
    gamely_img_on_fetch_cb  on_done;
    void                   *on_done_usr;
    uint8_t                *buf;
    size_t                  len;
    size_t                  cap;
    char                    hint[16];
    int                     failed;
} fetch_ctx_t;

static void on_status(gly_req_id_t id, int status, void *usr) {
    (void)id;
    fetch_ctx_t *ctx = (fetch_ctx_t *)usr;
    if (status < 200 || status >= 300) ctx->failed = 1;
}

static void on_data(gly_req_id_t id, const char *data, size_t len, void *usr) {
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

static void on_done(gly_req_id_t id, void *usr) {
    (void)id;
    fetch_ctx_t *ctx = (fetch_ctx_t *)usr;
    if (ctx->failed || !ctx->buf) {
        free(ctx->buf);
        ctx->on_done(NULL, 0, NULL, ctx->on_done_usr);
    } else {
        ctx->on_done(ctx->buf, ctx->len,
                     ctx->hint[0] ? ctx->hint : NULL,
                     ctx->on_done_usr);
        /* buf ownership transferred to Daemon_Img — it will call release(buf) */
    }
    free(ctx);
}

static void on_error(gly_req_id_t id, const char *msg, void *usr) {
    (void)id; (void)msg;
    fetch_ctx_t *ctx = (fetch_ctx_t *)usr;
    free(ctx->buf);
    ctx->on_done(NULL, 0, NULL, ctx->on_done_usr);
    free(ctx);
}

void gamely_resolver_image_http(const char *url, void *schema_usr,
                            gamely_img_on_fetch_cb img_on_done,
                            void *img_on_done_usr) {
    (void)schema_usr;

    fetch_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) { img_on_done(NULL, 0, NULL, img_on_done_usr); return; }

    ctx->on_done     = img_on_done;
    ctx->on_done_usr = img_on_done_usr;

    /* derive hint from URL extension */
    const char *dot = strrchr(url, '.');
    if (dot && dot[1] && !strchr(dot, '?') && !strchr(dot, '/')) {
        const char *q = strchr(dot, '?');
        size_t ext_len = q ? (size_t)(q - dot - 1) : strlen(dot + 1);
        if (ext_len < sizeof(ctx->hint)) {
            strncpy(ctx->hint, dot + 1, ext_len);
            ctx->hint[ext_len] = '\0';
        }
    }

    gamely_daemon_webclient_http(url, NULL, on_status, on_data, on_done, on_error, ctx);
}
