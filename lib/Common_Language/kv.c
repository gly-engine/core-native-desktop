#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gecnd.h"

bool gecnd_lang_kv_param(gecnd_lang_t *ctx) {
    ctx->finished = 1;

    const char *buf = ctx->pattern;
    const char *key = ctx->text;
    if (buf == NULL || key == NULL) {
        ctx->error = 1;
        return false;
    }

    size_t      klen  = strlen(key);
    bool        found = false;
    const char *p     = buf;
    while (*p != '\0') {
        size_t      kl = strlen(p);
        const char *v  = p + kl + 1;
        size_t      vl = strlen(v);
        if (kl == klen && memcmp(p, key, klen) == 0) {
            ctx->result.ptr = (void *)v;
            ctx->result.len = vl;
            found = true;
        }
        p = v + vl + 1;
    }

    if (!found) {
        ctx->error = 1;
        return false;
    }
    return true;
}
