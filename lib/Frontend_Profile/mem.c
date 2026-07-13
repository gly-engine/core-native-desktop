#include <stdio.h>
#include <string.h>
#include "gemetrics.h"

/* Memória do sistema (mesma fonte do `free`): percent/free/use/total.
 * Cache de 60s, igual ao temp.c. Retorna percent de uso ou -1. */

#if defined(__linux__)

static int platform_read_mem(uint32_t *free_mb, uint32_t *used_mb, uint32_t *total_mb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    uint64_t total_kb = 0, avail_kb = 0, free_kb = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%llu", (unsigned long long *)&total_kb);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, "%llu", (unsigned long long *)&avail_kb);
        else if (strncmp(line, "MemFree:", 8) == 0)
            sscanf(line + 8, "%llu", (unsigned long long *)&free_kb);
    }
    fclose(f);

    if (total_kb == 0) return -1;
    if (avail_kb == 0) avail_kb = free_kb; /* kernel antigo sem MemAvailable */

    uint64_t used_kb = total_kb - avail_kb;
    *free_mb  = (uint32_t)(avail_kb / 1024);
    *used_mb  = (uint32_t)(used_kb  / 1024);
    *total_mb = (uint32_t)(total_kb / 1024);
    return (int)((used_kb * 100) / total_kb);
}

#else

static int platform_read_mem(uint32_t *free_mb, uint32_t *used_mb, uint32_t *total_mb)
{
    (void)free_mb; (void)used_mb; (void)total_mb;
    return -1;
}

#endif

#define MEM_REFRESH_MS (60 * 1000)

static int      s_cached_percent = -1;
static uint32_t s_cached_free    = 0;
static uint32_t s_cached_used    = 0;
static uint32_t s_cached_total   = 0;
static uint64_t s_last_read_time = 0;

int gecnd_profile_get_mem(uint32_t *free_mb, uint32_t *used_mb, uint32_t *total_mb)
{
    uint64_t now = gecnd_get_cur_time();
    if (s_last_read_time == 0 || (now - s_last_read_time) >= MEM_REFRESH_MS) {
        s_cached_percent = platform_read_mem(&s_cached_free, &s_cached_used, &s_cached_total);
        s_last_read_time = now;
    }
    if (free_mb)  *free_mb  = s_cached_free;
    if (used_mb)  *used_mb  = s_cached_used;
    if (total_mb) *total_mb = s_cached_total;
    return s_cached_percent;
}
