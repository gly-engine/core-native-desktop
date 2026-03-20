#include <stdio.h>
#include "gemetrics.h"

#if defined(__linux__)

static int32_t platform_read_temp(void)
{
    char path[64];
    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path),
                 "/sys/class/thermal/thermal_zone%d/temp", i);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int32_t raw = 0;
        int ok = fscanf(f, "%d", &raw);
        fclose(f);
        if (ok == 1) return raw / 1000;
    }
    return -1;
}

#elif defined(__APPLE__)

static int32_t platform_read_temp(void)
{
    return -1;
}

#elif defined(_WIN32)

static int32_t platform_read_temp(void)
{
    return -1;
}

#else

static int32_t platform_read_temp(void)
{
    return -1;
}

#endif

#define TEMP_REFRESH_MS (60 * 1000)

static int32_t  s_cached_temp    = -1;
static uint64_t s_last_read_time = 0;

int32_t gecnd_profile_get_temp(void)
{
    uint64_t now = gecnd_get_cur_time();
    if (s_last_read_time == 0 || (now - s_last_read_time) >= TEMP_REFRESH_MS) {
        s_cached_temp    = platform_read_temp();
        s_last_read_time = now;
    }
    return s_cached_temp;
}
