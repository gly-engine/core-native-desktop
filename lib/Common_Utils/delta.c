#include <stdint.h>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <time.h>
#endif

uint32_t gecnd_get_delta_ms(void) {
    static uint64_t last_ms = 0;

    uint64_t now_ms;

#if defined(_WIN32)
    now_ms = GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now_ms = (uint64_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
#endif

    if (last_ms == 0) {
        last_ms = now_ms;
        return 16;
    }

    uint32_t delta = (uint32_t)(now_ms - last_ms);
    last_ms = now_ms;

    return delta;
}
