#include <stdint.h>

double __floatdidf(int64_t a) {
    return (double)a;
}

double __floatundidf(uint64_t a) {
    return (double)a;
}

float __floatdisf(int64_t a) {
    return (float)a;
}

float __floatundisf(uint64_t a) {
    return (float)a;
}

int64_t __fixdfdi(double a) {
    return (int64_t)a;
}

uint64_t __fixunsdfdi(double a) {
    return (uint64_t)a;
}

int64_t __fixsfdi(float a) {
    return (int64_t)a;
}

uint64_t __fixunssfdi(float a) {
    return (uint64_t)a;
}