#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gecnd.h"

/* ── file:// and "" schema handler ───────────────────────────────── */

typedef struct {
    gamely_img_on_fetch_cb  on_done;
    void                   *on_done_usr;
    char                    hint[16];
} read_ctx_t;

static void on_fs_read(uint8_t *data, size_t len, void *usr) {
    read_ctx_t *ctx = (read_ctx_t *)usr;
    if (!data) printf("[io-resolver] read failed\n");
    ctx->on_done(data, len, ctx->hint[0] ? ctx->hint : NULL, ctx->on_done_usr);
    free(ctx);
}

static void on_found(const char *path, void *usr) {
    read_ctx_t *ctx = (read_ctx_t *)usr;

    /* re-derive hint from found path's extension */
    const char *dot = strrchr(path, '.');
    if (dot && dot[1]) {
        strncpy(ctx->hint, dot + 1, sizeof(ctx->hint) - 1);
        ctx->hint[sizeof(ctx->hint) - 1] = '\0';
    }

    if (gamely_daemon_fs_read(path, NULL, NULL, on_fs_read, ctx) != 0) {
        printf("[io-resolver] read enqueue failed '%s'\n", path);
        ctx->on_done(NULL, 0, NULL, ctx->on_done_usr);
        free(ctx);
    }
}

void gamely_resolver_image_file(const char *url, void *schema_usr,
                          gamely_img_on_fetch_cb on_done, void *on_done_usr) {
    (void)schema_usr;

    /* strip file:// prefix */
    const char *path = strncmp(url, "file://", 7) == 0 ? url + 7 : url;

    /* derive hint from extension */
    char hint[16] = {0};
    const char *dot = strrchr(path, '.');
    if (dot && dot[1]) {
        strncpy(hint, dot + 1, sizeof(hint) - 1);
    }

    read_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) { on_done(NULL, 0, NULL, on_done_usr); return; }
    ctx->on_done     = on_done;
    ctx->on_done_usr = on_done_usr;
    strncpy(ctx->hint, hint, sizeof(ctx->hint) - 1);
    ctx->hint[sizeof(ctx->hint) - 1] = '\0';

    /* try direct async read first */
    if (gamely_daemon_fs_read(path, NULL, NULL, on_fs_read, ctx) == 0) return;

    /* fall back: search under cwd and exe_cwd */
    char cwd[512], exe[512];
    gecnd_utils_get_cwd(cwd, sizeof(cwd));
    gecnd_utils_get_exe_cwd(exe, sizeof(exe));

    const char *paths[] = { cwd, exe, NULL };
    const char *files[] = { path, NULL };
    if (gamely_daemon_fs_search(paths, files, NULL,
                                 GLY_FS_ONE_CB_ASYNC, on_found, ctx) == 0) return;

    printf("[io-resolver] not found '%s' (cwd=%s)\n", path, cwd);
    on_done(NULL, 0, NULL, on_done_usr);
    free(ctx);
}
