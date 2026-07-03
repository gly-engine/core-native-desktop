/**
 * @file plugins/libretro/url.c
 * @date 2026-06-30
 * @author Rodrigo Dornelles
 */
#include <stdlib.h>
#include <string.h>

#include "main.h"

static char *cache = NULL;

__attribute__((destructor))
static void url_env_clear(void) {
    free(cache);
    cache = NULL;
}

void url_env_set(const char *url) {
    free(cache);
    cache = NULL;
    if (url == NULL) {
        return;
    }

    size_t       length = 1;
    gecnd_lang_t ctx    = {{ "url", url }};
    while (api->lang(&ctx)) {
        if (ctx.url.kind == GECND_URL_KIND_PARAM) {
            length += ctx.url.len + 1 + ctx.url.val.len + 1;
        }
    }

    cache = malloc(length);
    if (cache == NULL) {
        return;
    }

    size_t index = 0;
    ctx.reset = 1;
    while (api->lang(&ctx)) {
        if (ctx.url.kind == GECND_URL_KIND_PARAM) {
            memcpy(cache + index, ctx.url.ptr, ctx.url.len);
            index += ctx.url.len;
            cache[index++] = '\0';
            if (ctx.url.val.len) {
                memcpy(cache + index, ctx.url.val.ptr, ctx.url.val.len);
                index += ctx.url.val.len;
            }
            cache[index++] = '\0';
        }
    }
    cache[index] = '\0';
}

const char *url_env_get(const char *key) {
    if (cache == NULL || key == NULL) {
        return NULL;
    }
    gecnd_lang_t ctx = {{ "kv:param", cache, key }};
    if (api->lang(&ctx)) {
        return ctx.result.ptr;
    }
    return NULL;
}
