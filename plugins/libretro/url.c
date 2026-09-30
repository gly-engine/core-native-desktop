/**
 * @file plugins/libretro/url.c
 * @date 2026-06-30
 * @author Rodrigo Dornelles
 */
#include <stdlib.h>
#include <string.h>

#include "main.h"

static char *cache_param = NULL;
static char *cache_opt   = NULL;

__attribute__((destructor))
static void url_env_clear(void) {
    free(cache_param);
    free(cache_opt);
    cache_param = NULL;
    cache_opt   = NULL;
}

static char *url_collect(const char *url, int kind) {
    size_t       length = 1;
    gecnd_lang_t ctx    = {{ "url", url }};
    while (api->lang(&ctx)) {
        if (ctx.url.kind == kind) {
            length += ctx.url.len + 1 + ctx.url.val.len + 1;
        }
    }

    char *out = malloc(length);
    if (out == NULL) {
        return NULL;
    }

    size_t index = 0;
    ctx.reset = 1;
    while (api->lang(&ctx)) {
        if (ctx.url.kind == kind) {
            memcpy(out + index, ctx.url.ptr, ctx.url.len);
            index += ctx.url.len;
            out[index++] = '\0';
            if (ctx.url.val.len) {
                memcpy(out + index, ctx.url.val.ptr, ctx.url.val.len);
                index += ctx.url.val.len;
            }
            out[index++] = '\0';
        }
    }
    out[index] = '\0';
    return out;
}

void url_env_set(const char *url) {
    free(cache_param);
    free(cache_opt);
    cache_param = NULL;
    cache_opt   = NULL;
    if (url == NULL) {
        return;
    }
    cache_param = url_collect(url, GECND_URL_KIND_PARAM);
    cache_opt   = url_collect(url, GECND_URL_KIND_FRAGMENT);
}

static const char *url_lookup(const char *cache, const char *key) {
    if (cache == NULL || key == NULL) {
        return NULL;
    }
    gecnd_lang_t ctx = {{ "kv:param", cache, key }};
    if (api->lang(&ctx)) {
        return ctx.result.ptr;
    }
    return NULL;
}

const char *url_env_get(const char *key) {
    return url_lookup(cache_param, key);
}

const char *url_opt_get(const char *key) {
    return url_lookup(cache_opt, key);
}
