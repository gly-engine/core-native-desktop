#include <stdint.h>

#if defined(GECND_LIBUV)
    #include <uv.h>
#elif defined(_WIN32)
    #include <windows.h>
#else
    #include <time.h>
#endif

uint64_t gecnd_get_cur_time(void) {
#if defined(GECND_LIBUV)
    return uv_hrtime() / 1000000;
#elif defined(_WIN32)
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
#endif
}

uint32_t gecnd_get_delta_ms(void) {
    static uint64_t last_ms = 0;
    uint64_t now_ms = gecnd_get_cur_time();

    if (last_ms == 0) {
        last_ms = now_ms;
        return 16;
    }

    uint32_t delta = (uint32_t)(now_ms - last_ms);
    last_ms = now_ms;

    return delta;
}
