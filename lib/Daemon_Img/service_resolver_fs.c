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

static const char *const s_fallbacks[] = {
    "etc1:etc1", "tga:rgba5551", "tga:rgba8888", "jpg:yuv420p", "jpeg:yuv420p", NULL
};

/* For a requested .png we may substitute a sibling in one of the fallback
 * formats (searched by `from`, the extension), in priority order; the
 * dispatcher then picks the actual decoder. */
void gamely_resolver_image_file(const char *url, void *schema_usr,
                          gamely_img_on_fetch_cb on_done, void *on_done_usr) {
    (void)schema_usr;
    const char *const *alts = s_fallbacks;

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

    const char *cwd = NULL, *exe = NULL;
    gecnd_registry("get", "cwd", &cwd, NULL);
    gecnd_registry("get", "pwd", &exe, NULL);
    if (!cwd) cwd = "";
    if (!exe) exe = "";

    /* PNG asked: prefer a sibling in one of the configured fallback formats
     * (e.g. .etc1/.tga/.jpeg), in priority order, skipping any without a usable
     * decoder. Falls through to the .png itself when none exist. */
    if (dot && alts && strcmp(hint, "png") == 0) {
        char        ext_buf[6][16];
        const char *exts[7];
        int         ne = 0;
        for (int i = 0; alts[i] && ne < 6; i++) {
            /* alt is "from:to" — search by `from`, the file extension. */
            const char *colon = strchr(alts[i], ':');
            size_t flen = colon ? (size_t)(colon - alts[i]) : strlen(alts[i]);
            char from[16];
            if (flen >= sizeof(from)) flen = sizeof(from) - 1;
            memcpy(from, alts[i], flen);
            from[flen] = '\0';
            if (!gamely_daemon_img_can_decode(from)) continue;
            snprintf(ext_buf[ne], sizeof(ext_buf[ne]), ".%s", from);
            exts[ne] = ext_buf[ne];
            ne++;
        }
        exts[ne] = NULL;

        char   base[512];
        size_t base_len = (size_t)(dot - path);
        if (ne > 0 && base_len < sizeof(base)) {
            memcpy(base, path, base_len);
            base[base_len] = '\0';

            const char *paths[] = { cwd, exe, NULL };
            const char *files[] = { base, NULL };
            if (gamely_daemon_fs_search(paths, files, exts,
                                         GLY_FS_ONE_CB_ASYNC, on_found, ctx) == 0)
                return;
        }
    }

    /* try direct async read first */
    if (gamely_daemon_fs_read(path, NULL, NULL, on_fs_read, ctx) == 0) return;

    /* fall back: search under cwd and exe_cwd */
    const char *paths[] = { cwd, exe, NULL };
    const char *files[] = { path, NULL };
    if (gamely_daemon_fs_search(paths, files, NULL,
                                 GLY_FS_ONE_CB_ASYNC, on_found, ctx) == 0) return;

    printf("[io-resolver] not found '%s' (cwd=%s)\n", path, cwd);
    on_done(NULL, 0, NULL, on_done_usr);
    free(ctx);
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_resolver:file$0", gamely_resolver_image_file, NULL);
    gecnd_registry("set", "image_resolver:$s",     gamely_resolver_image_file, NULL);
}
