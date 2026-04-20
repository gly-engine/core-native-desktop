#ifdef _WIN32
#  include <windows.h>
#else
#  include <glob.h>
#  include <dirent.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "gecnd.h"

#define PATH_CAP   512
#define ASYNC_CAP  128

/* ── async delivery queue (for GLY_FS_*_ASYNC modes) ─────────────── */

typedef struct { char *path; gamely_fs_cb cb; void *usr; } async_item_t;

static async_item_t s_async[ASYNC_CAP];
static int          s_async_head = 0;
static int          s_async_tail = 0;

static void async_push(const char *path, gamely_fs_cb cb, void *usr) {
    int next = (s_async_head + 1) % ASYNC_CAP;
    if (next == s_async_tail) return;
    s_async[s_async_head] = (async_item_t){ strdup(path), cb, usr };
    s_async_head = next;
}

void fs_search_async_drain(void) {
    while (s_async_tail != s_async_head) {
        async_item_t it = s_async[s_async_tail];
        s_async_tail = (s_async_tail + 1) % ASYNC_CAP;
        if (it.cb) it.cb(it.path, it.usr);
        free(it.path);
    }
}

/* ── search internals ─────────────────────────────────────────────── */

static char s_result[PATH_CAP];

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

typedef struct {
    gamely_fs_mode_t  mode;
    void             *a;
    void             *b;
    int               stop;
    int               found;
} sctx_t;

static void deliver(sctx_t *ctx, const char *path) {
    ctx->found++;
    switch (ctx->mode) {
    case GLY_FS_ONE:
        strncpy(s_result, path, PATH_CAP - 1);
        s_result[PATH_CAP - 1] = '\0';
        *(const char **)ctx->a = s_result;
        ctx->stop = 1;
        break;
    case GLY_FS_ONE_CB:
        ((gamely_fs_cb)ctx->a)(path, ctx->b);
        ctx->stop = 1;
        break;
    case GLY_FS_ALL_CB:
        ((gamely_fs_cb)ctx->a)(path, ctx->b);
        break;
    case GLY_FS_ONE_CB_ASYNC:
        async_push(path, (gamely_fs_cb)ctx->a, ctx->b);
        ctx->stop = 1;
        break;
    case GLY_FS_ALL_CB_ASYNC:
        async_push(path, (gamely_fs_cb)ctx->a, ctx->b);
        break;
    }
}

static void list_dir(const char *dir, const char **exts, sctx_t *ctx) {
#ifdef _WIN32
    char pattern[PATH_CAP];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (exts) {
            const char *dot = strrchr(fd.cFileName, '.');
            if (!dot) continue;
            int match = 0;
            for (int i = 0; exts[i]; i++)
                if (strcmp(dot, exts[i]) == 0) { match = 1; break; }
            if (!match) continue;
        }
        char path[PATH_CAP];
        snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
        deliver(ctx, path);
        if (ctx->stop) break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && !ctx->stop) {
        if (e->d_name[0] == '.') continue;
        if (exts) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot) continue;
            int match = 0;
            for (int i = 0; exts[i]; i++)
                if (strcmp(dot, exts[i]) == 0) { match = 1; break; }
            if (!match) continue;
        }
        char path[PATH_CAP];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        deliver(ctx, path);
    }
    closedir(d);
#endif
}

static void search_in_dir(const char *dir, const char **files,
                           const char **exts, sctx_t *ctx) {
    if (ctx->stop) return;
    if (!files) {
        if (file_exists(dir)) deliver(ctx, dir);
        return;
    }
    for (int fi = 0; files[fi] && !ctx->stop; fi++) {
        if (strcmp(files[fi], "*") == 0) {
            list_dir(dir, exts, ctx);
        } else if (!exts || !exts[0]) {
            char path[PATH_CAP];
            snprintf(path, sizeof(path), "%s/%s", dir, files[fi]);
            if (file_exists(path)) deliver(ctx, path);
        } else {
            for (int ei = 0; exts[ei] && !ctx->stop; ei++) {
                char path[PATH_CAP];
                snprintf(path, sizeof(path), "%s/%s%s", dir, files[fi], exts[ei]);
                if (file_exists(path)) deliver(ctx, path);
            }
        }
    }
}

#ifdef _WIN32
static int wildmatch(const char *pat, const char *str) {
    while (*pat && *str) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            while (*str) { if (wildmatch(pat, str)) return 1; str++; }
            return 0;
        }
        if (*pat != '?' && *pat != *str) return 0;
        pat++; str++;
    }
    while (*pat == '*') pat++;
    return !*pat && !*str;
}

static void win_glob(const char *pattern, const char **files,
                      const char **exts, sctx_t *ctx) {
    char tmp[PATH_CAP];
    strncpy(tmp, pattern, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    const char *parts[64];
    size_t n = 0;
    for (char *t = strtok(tmp, "/\\"); t && n < 64; t = strtok(NULL, "/\\"))
        parts[n++] = t;
    if (!n) return;

    char base[PATH_CAP] = {0};
    size_t wi = 0;
    for (; wi < n; wi++) {
        if (strchr(parts[wi], '*') || strchr(parts[wi], '?')) break;
        char next[PATH_CAP];
        if (base[0])
            snprintf(next, sizeof(next), "%s/%s", base, parts[wi]);
        else
            strncpy(next, parts[wi], sizeof(next) - 1);
        strncpy(base, next, sizeof(base) - 1);
    }
    if (wi == n) { search_in_dir(base, files, exts, ctx); return; }

    char search[PATH_CAP];
    snprintf(search, sizeof(search), "%s/*", base[0] ? base : ".");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (!wildmatch(parts[wi], fd.cFileName)) continue;
        char next[PATH_CAP];
        snprintf(next, sizeof(next), "%s/%s", base[0] ? base : ".", fd.cFileName);
        if (wi + 1 < n) {
            char sub[PATH_CAP];
            strncpy(sub, next, sizeof(sub) - 1);
            for (size_t si = wi + 1; si < n; si++) {
                strncat(sub, "/",       sizeof(sub) - strlen(sub) - 1);
                strncat(sub, parts[si], sizeof(sub) - strlen(sub) - 1);
            }
            win_glob(sub, files, exts, ctx);
        } else {
            search_in_dir(next, files, exts, ctx);
        }
        if (ctx->stop) break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#endif

static void expand_and_search(const char *pattern, const char **files,
                                const char **exts, sctx_t *ctx) {
    if (ctx->stop) return;
    int has_glob = !!(strchr(pattern, '*') || strchr(pattern, '?'));
    if (!has_glob) {
        search_in_dir(pattern, files, exts, ctx);
        return;
    }
#ifdef _WIN32
    win_glob(pattern, files, exts, ctx);
#else
    glob_t g;
    if (glob(pattern, GLOB_NOSORT, NULL, &g) != 0) return;
    for (size_t i = 0; i < g.gl_pathc && !ctx->stop; i++)
        search_in_dir(g.gl_pathv[i], files, exts, ctx);
    globfree(&g);
#endif
}

/* ── public search entry point (called by FS drivers) ────────────── */

int fs_search_execute(const char **paths, const char **files,
                       const char **exts,  gamely_fs_mode_t mode,
                       void *a, void *b) {
    if (!paths || !a) return -1;
    sctx_t ctx = { mode, a, b, 0, 0 };
    for (int i = 0; paths[i] && !ctx.stop; i++)
        expand_and_search(paths[i], files, exts, &ctx);
    return ctx.found > 0 ? 0 : -1;
}
