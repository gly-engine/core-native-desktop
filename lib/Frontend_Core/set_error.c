#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "gecnd.h"

void gecnd_add_error(gecnd_t *gly, const char *fmt, ...) {
    if (!gly) return;

    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) return;

    size_t sep   = gecnd_has_errors(gly) ? 1 : 0;
    size_t total = gly->error_len + sep + (size_t)need + 1;

    if (total > gly->error_cap) {
        size_t cap = gly->error_cap ? gly->error_cap * 2 : 128;
        while (cap < total) cap *= 2;
        char *nb = realloc(gly->error_buf, cap);
        if (!nb) return;
        gly->error_buf = nb;
        gly->error_cap = cap;
    }

    if (sep) gly->error_buf[gly->error_len++] = '\n';

    va_start(ap, fmt);
    int written = vsnprintf(gly->error_buf + gly->error_len,
                            gly->error_cap  - gly->error_len, fmt, ap);
    va_end(ap);

    if (written > 0) gly->error_len += (size_t)written;
}
