#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

bool gecnd_lang_rdsl_iterator(gecnd_lang_t *ctx);
bool gecnd_lang_url_iterator(gecnd_lang_t *ctx);
bool gecnd_lang_url_location(gecnd_lang_t *ctx);
bool gecnd_lang_url_param(gecnd_lang_t *ctx);
bool gecnd_lang_file_ext(gecnd_lang_t *ctx);
bool gecnd_lang_kv_param(gecnd_lang_t *ctx);

static const struct lang_entry {
    const char *name;
    bool      (*fn)(gecnd_lang_t *);
} LANGS[] = {
    { "file:ext",     gecnd_lang_file_ext      },
    { "kv:param",     gecnd_lang_kv_param      },
    { "rdsl",         gecnd_lang_rdsl_iterator },
    { "url",          gecnd_lang_url_iterator  },
    { "url:location", gecnd_lang_url_location  },
    { "url:param",    gecnd_lang_url_param     },
};

static int lang_cmp(const void *key, const void *el) {
    return strcmp((const char *)key, ((const struct lang_entry *)el)->name);
}

bool gecnd_lang(gecnd_lang_t *const ctx) {
    if (ctx->reset) {
        ctx->reset    = 0;
        ctx->error    = 0;
        ctx->finished = 0;
        memset(&ctx->url, 0, sizeof ctx->url);
    }

    if (ctx->finished) {
        ctx->error = 1;
        return false;
    }

    if (!ctx->started) {
        ctx->started = 1;
        const struct lang_entry *e = ctx->_lang
            ? bsearch(ctx->_lang, LANGS, sizeof(LANGS) / sizeof(*LANGS),
                      sizeof(*LANGS), lang_cmp)
            : NULL;
        if (e == NULL) {
            ctx->finished = 1;
            ctx->error    = 1;
            return false;
        }
        ctx->_fn = e->fn;
    }

    bool more = ctx->_fn(ctx);
    if (!more) {
        ctx->finished = 1;
    }
    return more;
}
