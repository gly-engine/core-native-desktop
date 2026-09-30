#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static void get_pwd(char *buffer, size_t max_size) {
    if (!buffer || max_size == 0)
        return;
    buffer[0] = '\0';

#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD len = GetModuleFileNameA(NULL, buffer, (DWORD)max_size);
    if (len == 0 || len >= max_size) { buffer[0] = '\0'; return; }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", buffer, max_size - 1);
    if (len <= 0 || len >= (ssize_t)max_size) { buffer[0] = '\0'; return; }
    buffer[len] = '\0';
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)max_size;
    if (_NSGetExecutablePath(buffer, &size) != 0) { buffer[0] = '\0'; return; }
#else
    return;
#endif

    char *last_sep =
#if defined(_WIN32)
        strrchr(buffer, '\\');
#else
        strrchr(buffer, '/');
#endif
    if (last_sep)
        *last_sep = '\0';
}

static void get_cwd(char *buffer, size_t max_size) {
    if (!buffer || max_size == 0)
        return;
    buffer[0] = '\0';

#if defined(_WIN32)
    DWORD len = GetCurrentDirectoryA((DWORD)max_size, buffer);
    if (len == 0 || len >= max_size) buffer[0] = '\0';
#else
    if (getcwd(buffer, max_size) == NULL)
        buffer[0] = '\0';
#endif
}

/**
 * @todo temporary stub for microslop ruindows :/
 */
#if defined(_WIN32)
#include <stdint.h>
void native_text_font_previous() {}
#endif

__attribute__((constructor))
static void init(void) {
    char path[2048];

    get_cwd(path, sizeof(path));
    gecnd_registry("set", "cwd", path, "strdup=val,readonly=1");

    get_pwd(path, sizeof(path));
    gecnd_registry("set", "pwd", path, "strdup=val,readonly=1");
}
