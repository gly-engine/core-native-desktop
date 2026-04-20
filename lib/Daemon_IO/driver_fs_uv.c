#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <uv.h>
#include "gecnd.h"

/* ── service_search.c prototypes ──────────────────────────────────── */
int  fs_search_execute    (const char **paths, const char **files,
                            const char **exts,  gamely_fs_mode_t mode,
                            void *a, void *b);
void fs_search_async_drain(void);

/* ── async read via libuv thread pool ─────────────────────────────── */

static uv_loop_t *s_loop = NULL;

typedef struct {
    uv_work_t         work;
    char             *path;
    uint8_t          *data;
    size_t            len;
    gamely_fs_read_cb on_done;
    void             *usr;
} read_work_t;

static uint8_t *read_file(const char *path, size_t *out_len) {
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf)  { fclose(f); return NULL; }
    *out_len = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    return buf;
}

static void work_cb(uv_work_t *req) {
    read_work_t *w = (read_work_t *)req;
    w->data = read_file(w->path, &w->len);
}

static void after_work_cb(uv_work_t *req, int status) {
    read_work_t *w = (read_work_t *)req;
    (void)status;
    if (!w->data) printf("[fs-uv] read failed '%s'\n", w->path);
    if (w->on_done) w->on_done(w->data, w->len, w->usr);
    free(w->path);
    free(w);
}

/* ── public API ───────────────────────────────────────────────────── */

void gamely_daemon_fs_start(void *loop) { s_loop = (uv_loop_t *)loop; }
void gamely_daemon_fs_stop (void)       { s_loop = NULL; }

int gamely_daemon_fs_search(const char **paths, const char **files,
                             const char **exts,  gamely_fs_mode_t mode,
                             void *a, void *b) {
    return fs_search_execute(paths, files, exts, mode, a, b);
}

int gamely_daemon_fs_read(const char *path, uint8_t **out_data, size_t *out_len,
                           gamely_fs_read_cb on_done, void *usr) {
    if (!path) return -1;
    if (!on_done) {
        if (!out_data || !out_len) return -1;
        *out_data = read_file(path, out_len);
        return *out_data ? 0 : -1;
    }
    if (!s_loop) { on_done(NULL, 0, usr); return 0; }
    read_work_t *w = calloc(1, sizeof(*w));
    if (!w) { on_done(NULL, 0, usr); return 0; }
    w->path   = strdup(path);
    w->on_done = on_done;
    w->usr    = usr;
    uv_queue_work(s_loop, &w->work, work_cb, after_work_cb);
    return 0;
}

/* uv event loop drives after_work_cb delivery — only drain search queue */
void gamely_daemon_fs_tick(void) { fs_search_async_drain(); }
