#ifdef _WIN32
#  include <windows.h>
#else
#  include <glob.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "main.h"

#define PATH_CAP 1024

static const char *s_core_dirs[] = {
    "/mnt/*/*/libretro",
    "/usr/lib/libretro",
    "/usr/local/lib/libretro",
    ".var/app/org.libretro.RetroArch/config/retroarch/cores",
    ".config/retroarch/cores",
};

static const char *s_rom_dirs[] = {
    "/mnt/*/*/roms",
    "/mnt/*/*/libretro/roms",
    "RetroArch/downloads",
    "Downloads",
};

static char s_found[PATH_CAP];

static struct {
    const char *cwd;
    const char *pwd;
} paths;

static bool paths_bind(void) {
    if (paths.cwd) return true;
    api->registry("get", "cwd", (void *)&paths.cwd, NULL);
    api->registry("get", "pwd", (void *)&paths.pwd, NULL);
    return paths.cwd != NULL;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
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

static const char *win_find(const char *cur, const char **parts,
                              size_t nparts, size_t depth, const char *file) {
    if (depth == nparts) {
        snprintf(s_found, PATH_CAP, "%s/%s", cur, file);
        return file_exists(s_found) ? s_found : NULL;
    }
    const char *part = parts[depth];
    char next[PATH_CAP];
    if (!strchr(part, '*') && !strchr(part, '?')) {
        snprintf(next, sizeof(next), cur[0] ? "%s/%s" : "%s", cur, part);
        return win_find(next, parts, nparts, depth + 1, file);
    }
    char search[PATH_CAP];
    snprintf(search, sizeof(search), cur[0] ? "%s/*" : "*", cur);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (!wildmatch(part, fd.cFileName)) continue;
        snprintf(next, sizeof(next), cur[0] ? "%s/%s" : "%s", cur, fd.cFileName);
        const char *found = win_find(next, parts, nparts, depth + 1, file);
        if (found) { FindClose(h); return found; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return NULL;
}

static const char *glob_find(const char *dir_pattern, const char *file) {
    char tmp[PATH_CAP];
    snprintf(tmp, sizeof(tmp), "%s", dir_pattern);
    const char *parts[64];
    size_t n = 0;
    char *tok = strtok(tmp, "/\\");
    while (tok && n < 64) { parts[n++] = tok; tok = strtok(NULL, "/\\"); }
    char base[PATH_CAP] = {0};
    size_t start = 0;
    if (n > 0 && strlen(parts[0]) == 2 && parts[0][1] == ':') {
        snprintf(base, sizeof(base), "%s", parts[0]);
        start = 1;
    }
    return win_find(base, parts + start, n - start, 0, file);
}

#else

static const char *glob_find(const char *dir_pattern, const char *file) {
    glob_t g;
    if (glob(dir_pattern, GLOB_NOSORT, NULL, &g) != 0) return NULL;
    const char *found = NULL;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        snprintf(s_found, PATH_CAP, "%s/%s", g.gl_pathv[i], file);
        if (file_exists(s_found)) { found = s_found; break; }
    }
    globfree(&g);
    return found;
}

#endif

const char *scanner_resolve_core(const char *name) {
    if (!name || !name[0]) return NULL;
    if ((name[0] == '/' || (name[0] && name[1] == ':')) && file_exists(name))
        return name;

    char v1[PATH_CAP], v2[PATH_CAP];
    snprintf(v1, PATH_CAP, "%s_libretro.so", name);
    snprintf(v2, PATH_CAP, "lib%s_libretro.so", name);
    const char *variants[] = { name, v1, v2 };

    const char *cfg = NULL;
    api->registry("get", "libretro:core:path", (void *)&cfg, NULL);
    if (cfg && cfg[0]) {
        for (size_t v = 0; v < 3; v++) {
            snprintf(s_found, PATH_CAP, "%s/%s", cfg, variants[v]);
            if (file_exists(s_found)) return s_found;
        }
    }

    const char *cwd = "", *exedir = "";
    char        homedir[PATH_CAP];
    if (paths_bind()) {
        cwd    = paths.cwd ? paths.cwd : "";
        exedir = paths.pwd ? paths.pwd : "";
    }

    const char *home = getenv("HOME");
    if (home) strncpy(homedir, home, PATH_CAP - 1);
    else homedir[0] = '\0';

    const char *bases[] = { cwd, exedir, homedir };

    for (size_t d = 0; d < 3; d++) {
        if (!bases[d][0]) continue;
        for (size_t v = 0; v < 3; v++) {
            snprintf(s_found, PATH_CAP, "%s/%s", bases[d], variants[v]);
            if (file_exists(s_found)) return s_found;
        }
    }

    for (size_t i = 0; i < sizeof(s_core_dirs) / sizeof(*s_core_dirs); i++) {
        if (s_core_dirs[i][0] == '/') {
            for (size_t v = 0; v < 3; v++) {
                const char *f = glob_find(s_core_dirs[i], variants[v]);
                if (f) return f;
            }
        } else if (home) {
            char pattern[PATH_CAP];
            snprintf(pattern, PATH_CAP, "%s/%s", home, s_core_dirs[i]);
            for (size_t v = 0; v < 3; v++) {
                const char *f = glob_find(pattern, variants[v]);
                if (f) return f;
            }
        }
    }

    return NULL;
}

const char *scanner_resolve_rom(const char *name) {
    if (!name || !name[0]) return NULL;
    if ((name[0] == '/' || (name[0] && name[1] == ':')) && file_exists(name))
        return name;

    const char *cwd = "", *exedir = "";
    char        homedir[PATH_CAP];
    if (paths_bind()) {
        cwd    = paths.cwd ? paths.cwd : "";
        exedir = paths.pwd ? paths.pwd : "";
    }

    const char *home = getenv("HOME");
    if (home) strncpy(homedir, home, PATH_CAP - 1);
    else homedir[0] = '\0';

    const char *bases[] = { cwd, exedir, homedir };

    for (size_t d = 0; d < 3; d++) {
        if (!bases[d][0]) continue;
        snprintf(s_found, PATH_CAP, "%s/%s", bases[d], name);
        if (file_exists(s_found)) return s_found;
    }

    for (size_t i = 0; i < sizeof(s_rom_dirs) / sizeof(*s_rom_dirs); i++) {
        if (s_rom_dirs[i][0] == '/') {
            const char *f = glob_find(s_rom_dirs[i], name);
            if (f) return f;
        } else if (home) {
            char pattern[PATH_CAP];
            snprintf(pattern, PATH_CAP, "%s/%s", home, s_rom_dirs[i]);
            const char *f = glob_find(pattern, name);
            if (f) return f;
        }
    }

    return NULL;
}
