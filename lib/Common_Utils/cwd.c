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

size_t gecnd_utils_get_exe_cwd(char *buffer, size_t max_size) {
    if (!buffer || max_size == 0)
        return (size_t)-1;

#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD len = GetModuleFileNameA(NULL, buffer, (DWORD)max_size);
    if (len == 0 || len >= max_size)
        return (size_t)-1;
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", buffer, max_size - 1);
    if (len <= 0 || len >= (ssize_t)max_size)
        return (size_t)-1;
    buffer[len] = '\0';
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)max_size;
    if (_NSGetExecutablePath(buffer, &size) != 0)
        return (size_t)-1;
#else
    return (size_t)-1;
#endif

    char *last_sep =
#if defined(_WIN32)
        strrchr(buffer, '\\');
#else
        strrchr(buffer, '/');
#endif
    if (last_sep)
        *last_sep = '\0';

    return (size_t)strlen(buffer);
}

size_t gecnd_utils_get_cwd(char *buffer, size_t max_size) {
    if (!buffer || max_size == 0)
        return (size_t)-1;

#if defined(_WIN32)
    DWORD len = GetCurrentDirectoryA((DWORD)max_size, buffer);
    if (len == 0 || len >= max_size)
        return (size_t)-1;
#else
    if (getcwd(buffer, max_size) == NULL)
        return (size_t)-1;
#endif

    return (size_t)strlen(buffer);
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
    gecnd_registry("set", "function:gecnd_utils_get_cwd",     (void *)gecnd_utils_get_cwd,     NULL);
    gecnd_registry("set", "function:gecnd_utils_get_exe_cwd", (void *)gecnd_utils_get_exe_cwd, NULL);
}
