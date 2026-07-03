#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gecnd.h"

bool gecnd_lang_url_iterator(gecnd_lang_t *ctx);

bool gecnd_lang_url_location(gecnd_lang_t *ctx) {
    const char  *start = NULL;
    const char  *end   = NULL;
    gecnd_lang_t inner = {0};
    inner.pattern = ctx->pattern;

    while (gecnd_lang_url_iterator(&inner)) {
        if (inner.url.kind == GECND_URL_KIND_HOST) {
            start = inner.url.ptr;
            end   = inner.url.ptr + inner.url.len;
        } else if (inner.url.kind == GECND_URL_KIND_PATH) {
            if (start == NULL) {
                start = inner.url.ptr;
            }
            end = inner.url.ptr + inner.url.len;
        }
    }

    ctx->finished = 1;
    if (start == NULL) {
        ctx->error = 1;
        return false;
    }
    ctx->result.ptr = (void *)start;
    ctx->result.len = (size_t)(end - start);
    return true;
}

bool gecnd_lang_url_param(gecnd_lang_t *ctx) {
    ctx->finished = 1;

    size_t klen = ctx->text ? strlen(ctx->text) : 0;
    if (klen == 0) {
        ctx->error = 1;
        return false;
    }

    gecnd_lang_t inner = {0};
    inner.pattern = ctx->pattern;
    while (gecnd_lang_url_iterator(&inner)) {
        if (inner.url.kind == GECND_URL_KIND_PARAM
         && inner.url.len == klen
         && memcmp(inner.url.ptr, ctx->text, klen) == 0) {
            ctx->result = inner.url.val;
            return true;
        }
    }

    ctx->error = 1;
    return false;
}
