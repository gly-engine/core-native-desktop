#include <stdio.h>
#include <stdlib.h>
#include "gecnd.h"

#define PENDING_CAP 32

/* ── service_search.c prototypes ──────────────────────────────────── */
int  fs_search_execute    (const char **paths, const char **files,
                            const char **exts,  gamely_fs_mode_t mode,
                            void *a, void *b);
void fs_search_async_drain(void);

/* ── async read pending queue ─────────────────────────────────────── */

typedef struct {
    uint8_t          *data;
    size_t            len;
    gamely_fs_read_cb on_done;
    void             *usr;
} pending_t;

static pending_t s_pending[PENDING_CAP];
static int       s_head = 0;
static int       s_tail = 0;

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

/* ── public API ───────────────────────────────────────────────────── */

void gamely_daemon_fs_start(void *loop) { (void)loop; }
void gamely_daemon_fs_stop (void)       { s_head = s_tail = 0; }

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
    size_t   len  = 0;
    uint8_t *data = read_file(path, &len);
    int next = (s_head + 1) % PENDING_CAP;
    if (next == s_tail) { on_done(data, len, usr); return 0; }
    s_pending[s_head] = (pending_t){ data, len, on_done, usr };
    s_head = next;
    return 0;
}

void gamely_daemon_fs_tick(void) {
    while (s_tail != s_head) {
        pending_t p = s_pending[s_tail];
        s_tail = (s_tail + 1) % PENDING_CAP;
        if (p.on_done) p.on_done(p.data, p.len, p.usr);
    }
    fs_search_async_drain();
}
