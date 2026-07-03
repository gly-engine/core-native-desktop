#include <stdbool.h>
#include <stddef.h>

#include "gecnd.h"

bool gecnd_lang_file_ext(gecnd_lang_t *ctx) {
    ctx->finished = 1;

    const char *s = ctx->pattern;
    if (s == NULL) {
        ctx->error = 1;
        return false;
    }

    const char *dot = NULL;
    const char *p   = s;
    for (; *p != '\0'; p++) {
        if (*p == '/') {
            dot = NULL;
        } else if (*p == '.') {
            dot = p;
        }
    }

    if (dot != NULL && dot[1] != '\0') {
        ctx->result.ptr = (void *)(dot + 1);
        ctx->result.len = (size_t)(p - dot - 1);
        return true;
    }

    ctx->error = 1;
    return false;
}
