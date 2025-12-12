#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

//! @cond
#define PATH_MAX_SIZE (1024 - 8)
//! @endcond

const char *gecnd_utils_get_exe_cwd() {
    static char path[PATH_MAX_SIZE + 8];
    static bool initialized = false;
    if (initialized)
        return path[0] ? path : NULL;

#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
    if (len == 0 || len >= sizeof(path))
        return NULL;
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0 || len >= (ssize_t)sizeof(path))
        return NULL;
    path[len] = '\0';
#elif defined(__APPLE__)
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        return NULL;
#else
    return NULL;
#endif

    char *last_sep =
#if defined(_WIN32)
        strrchr(path, '\\');
#else
        strrchr(path, '/');
#endif
    if (last_sep)
        *last_sep = '\0';

    initialized = true;
    return (const char*) (path[0] ? path : NULL);
}

const char *gecnd_utils_get_cwd() {
    static char cwd[PATH_MAX_SIZE + 8];

#if defined(_WIN32)
    DWORD len = GetCurrentDirectoryA(sizeof(cwd), cwd);
    if (len == 0 || len >= sizeof(cwd))
        return NULL;
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return NULL;
#endif

    return (const char*) cwd;
}
