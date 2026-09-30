#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gecnd.h"

/* ── file:// and "" schema handler ───────────────────────────────── */

typedef struct {
    gamely_font_on_fetch_cb  on_done;
    void                    *on_done_usr;
} read_ctx_t;

static void on_fs_read(uint8_t *data, size_t len, void *usr) {
    read_ctx_t *ctx = (read_ctx_t *)usr;
    if (!data) printf("[font-resolver] read failed\n");
    ctx->on_done(data, len, NULL, ctx->on_done_usr);
    free(ctx);
}

static void on_found(const char *path, void *usr) {
    read_ctx_t *ctx = (read_ctx_t *)usr;
    if (gamely_daemon_fs_read(path, NULL, NULL, on_fs_read, ctx) != 0) {
        printf("[font-resolver] read enqueue failed '%s'\n", path);
        ctx->on_done(NULL, 0, NULL, ctx->on_done_usr);
        free(ctx);
    }
}

void gamely_resolver_font_file(const char *url, void *schema_usr,
                          gamely_font_on_fetch_cb on_done, void *on_done_usr) {
    (void)schema_usr;

    const char *path = strncmp(url, "file://", 7) == 0 ? url + 7 : url;

    read_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) { on_done(NULL, 0, NULL, on_done_usr); return; }
    ctx->on_done     = on_done;
    ctx->on_done_usr = on_done_usr;

    if (gamely_daemon_fs_read(path, NULL, NULL, on_fs_read, ctx) == 0) return;

    const char *cwd = NULL, *exe = NULL;
    gecnd_registry("get", "cwd", &cwd, NULL);
    gecnd_registry("get", "pwd", &exe, NULL);
    if (!cwd) cwd = "";
    if (!exe) exe = "";

    const char *paths[] = { cwd, exe, NULL };
    const char *files[] = { path, NULL };
    if (gamely_daemon_fs_search(paths, files, NULL,
                                 GLY_FS_ONE_CB_ASYNC, on_found, ctx) == 0) return;

    printf("[font-resolver] not found '%s' (cwd=%s)\n", path, cwd);
    on_done(NULL, 0, NULL, on_done_usr);
    free(ctx);
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "font_resolver:file$0", gamely_resolver_font_file, NULL);
    gecnd_registry("set", "font_resolver:$s",     gamely_resolver_font_file, NULL);
}
