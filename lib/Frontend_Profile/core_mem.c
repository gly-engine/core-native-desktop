#include <string.h>
#include "gemetrics.h"

/* Memória do processo atual: used (RSS/working set) / reserved (virtual) /
 * peak (maior RSS desde o início). Mesmo cache de 60s do mem.c (sys). */

#if defined(_WIN32)
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <stdio.h>
#endif

#if defined(_WIN32)

static int platform_read_core_mem(uint64_t *used, uint64_t *reserved, uint64_t *peak)
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc)))
        return -1;
    *used     = (uint64_t)pmc.WorkingSetSize;
    *reserved = (uint64_t)pmc.PagefileUsage;
    *peak     = (uint64_t)pmc.PeakWorkingSetSize;
    return 0;
}

#elif defined(__APPLE__)

static int platform_read_core_mem(uint64_t *used, uint64_t *reserved, uint64_t *peak)
{
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
        return -1;
    *used     = (uint64_t)info.resident_size;
    *reserved = (uint64_t)info.virtual_size;
    *peak     = (uint64_t)info.resident_size_max;
    return 0;
}

#elif defined(__linux__)

static int platform_read_core_mem(uint64_t *used, uint64_t *reserved, uint64_t *peak)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;

    uint64_t rss_kb = 0, vsize_kb = 0, hwm_kb = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0)
            sscanf(line + 6, "%llu", (unsigned long long *)&rss_kb);
        else if (strncmp(line, "VmSize:", 7) == 0)
            sscanf(line + 7, "%llu", (unsigned long long *)&vsize_kb);
        else if (strncmp(line, "VmHWM:", 6) == 0)
            sscanf(line + 6, "%llu", (unsigned long long *)&hwm_kb);
    }
    fclose(f);

    if (rss_kb == 0 && vsize_kb == 0) return -1;
    *used     = rss_kb   * 1024;
    *reserved = vsize_kb * 1024;
    *peak     = hwm_kb   * 1024;
    return 0;
}

#else

static int platform_read_core_mem(uint64_t *used, uint64_t *reserved, uint64_t *peak)
{
    (void)used; (void)reserved; (void)peak;
    return -1;
}

#endif

#define CORE_MEM_REFRESH_MS (60 * 1000)

static int      s_cached_ok      = -1;
static uint64_t s_cached_used    = 0;
static uint64_t s_cached_reserved = 0;
static uint64_t s_cached_peak    = 0;
static uint64_t s_last_read_time = 0;

int gecnd_profile_get_core_mem(uint64_t *used, uint64_t *reserved, uint64_t *peak)
{
    uint64_t now = gecnd_get_cur_time();
    if (s_last_read_time == 0 || (now - s_last_read_time) >= CORE_MEM_REFRESH_MS) {
        s_cached_ok = platform_read_core_mem(&s_cached_used, &s_cached_reserved, &s_cached_peak);
        s_last_read_time = now;
    }
    if (used)     *used     = s_cached_used;
    if (reserved) *reserved = s_cached_reserved;
    if (peak)     *peak     = s_cached_peak;
    return s_cached_ok;
}
