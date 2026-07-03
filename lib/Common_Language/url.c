/**
 * @file lib/Common_Language/url_iterator.c
 *
 * @brief URL iterator: walks a URL one component token at a time, exposing the
 *        same cursor-in-ctx shape as the rdsl iterator (see rdsl.c).
 *
 * @startebnf
 * url      = [ scheme, { "+", scheme }, "://", [ host ], [ ":", port ] ],
 *            { "/", segment }, [ "?", query ], [ "#", fragment ];
 * scheme   = alpha, { alpha | digit | "-" | "." };
 * host     = { ? any char except ":/?#" ? };
 * port     = digit, { digit };
 * segment  = { ? any char except "/?#" ? };
 * query    = pair, { "&", pair };
 * fragment = pair, { "&", pair };
 * pair     = key, [ "=", value ];
 * @endebnf
 *
 * Token kinds (see gecnd_lang_url_kind_t), with `idx` as the 0-based ordinal
 * within the kind:
 *   SCHEME    one '+'-joined scheme per idx, before "://" ("tic80+http" yields
 *             "tic80" idx 0, "http" idx 1)
 *   HOST      the host/authority after "://"
 *   PORT      the numeric port after the host's ':'
 *   PATH      one '/'-separated segment per idx
 *   PARAM     one '?'/'&' query pair per idx; key in ptr/len, value in val
 *   FRAGMENT  one '#'/'&' fragment pair per idx; key in ptr/len, value in val
 *
 * A bare path (no "://") yields PATH/PARAM/FRAGMENT only. `error` is raised on
 * a malformed component (non-numeric/out-of-range port, query/fragment pair
 * with an empty key); parsing still continues past it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gecnd.h"

enum {
    URL_P_SCHEME = 0,
    URL_P_HOST,
    URL_P_PORT,
    URL_P_PATH,
    URL_P_QUERY,
    URL_P_FRAGMENT,
    URL_P_DONE,
};

static bool url_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool url_is_schar(char c) {
    return url_is_alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
}

/* If s starts with a (possibly '+'-joined) scheme chain ending in "://",
 * returns the pointer to that "://"; otherwise NULL. */
static const char *url_scheme_end(const char *s) {
    const char *q = s;
    for (;;) {
        if (!url_is_alpha(*q)) {
            return NULL;            /* each scheme segment must start alpha */
        }
        q++;
        while (url_is_schar(*q)) {
            q++;
        }
        if (*q == '+') {
            q++;                    /* another '+'-joined scheme follows */
            continue;
        }
        return (q[0] == ':' && q[1] == '/' && q[2] == '/') ? q : NULL;
    }
}

static uint8_t url_after(char c) {
    switch (c) {
        case '/': return URL_P_PATH;
        case '?': return URL_P_QUERY;
        case '#': return URL_P_FRAGMENT;
        default:  return URL_P_DONE;   /* '\0' */
    }
}

static void url_token(gecnd_lang_t *const ctx, gecnd_lang_url_kind_t kind,
                      const char *ptr, size_t len) {
    ctx->url.idx     = (kind == ctx->url.kind) ? (int8_t)(ctx->url.idx + 1) : 0;
    ctx->url.kind    = kind;
    ctx->url.ptr     = ptr;
    ctx->url.len     = len;
    ctx->url.val.ptr = NULL;
    ctx->url.val.len = 0;
}

bool gecnd_lang_url_iterator(gecnd_lang_t *const ctx) {
    const char *url = ctx->pattern;

    if (ctx->url.cur == NULL) {
        if (url == NULL) {
            return false;
        }
        ctx->url.cur = url;
        ctx->error   = false;
    }

    for (;;) {
        const char *s = ctx->url.cur;

        switch (ctx->url.phase) {
        case URL_P_SCHEME: {
            if (url_scheme_end(s) == NULL) {
                ctx->url.phase = URL_P_PATH;   /* no "://" → bare path */
                continue;
            }
            const char *q = s + 1;   /* s[0] is alpha (url_scheme_end checked) */
            while (url_is_schar(*q)) {
                q++;
            }
            if (*q == '+') {
                ctx->url.cur = q + 1;          /* next '+'-joined scheme */
            } else {
                ctx->url.cur   = q + 3;        /* skip "://" */
                ctx->url.phase = URL_P_HOST;
            }
            url_token(ctx, GECND_URL_KIND_SCHEME, s, (size_t)(q - s));
            return true;
        }

        case URL_P_HOST: {
            const char *q = s;
            while (*q && *q != ':' && *q != '/' && *q != '?' && *q != '#') {
                q++;
            }
            ctx->url.cur   = q;
            ctx->url.phase = (*q == ':') ? URL_P_PORT : url_after(*q);
            if (q == s) {
                continue;   /* empty authority (e.g. "file:///") → no host */
            }
            url_token(ctx, GECND_URL_KIND_HOST, s, (size_t)(q - s));
            return true;
        }

        case URL_P_PORT: {
            const char *q = s + 1;   /* skip ':' */
            const char *d = q;
            uint32_t    v = 0;
            while (*d && *d != '/' && *d != '?' && *d != '#') {
                if (*d < '0' || *d > '9') {
                    ctx->error = true;
                } else if (v <= 65535) {
                    v = v * 10 + (uint32_t)(*d - '0');
                }
                d++;
            }
            if (d == q || v > 65535) {
                ctx->error = true;
            }
            ctx->url.cur   = d;
            ctx->url.phase = url_after(*d);
            url_token(ctx, GECND_URL_KIND_PORT, q, (size_t)(d - q));
            return true;
        }

        case URL_P_PATH: {
            const char *q = s;
            if (*q == '/') {
                q++;             /* consume one separator */
            }
            const char *seg = q;
            while (*q && *q != '/' && *q != '?' && *q != '#') {
                q++;
            }
            ctx->url.cur   = q;
            ctx->url.phase = url_after(*q);
            if (q == seg) {
                continue;        /* empty segment ("//", trailing '/') */
            }
            url_token(ctx, GECND_URL_KIND_PATH, seg, (size_t)(q - seg));
            return true;
        }

        case URL_P_QUERY:
        case URL_P_FRAGMENT: {
            bool query = (ctx->url.phase == URL_P_QUERY);
            char open  = query ? '?' : '#';
            const char *q = s;

            if (*q == open || *q == '&') {
                q++;             /* consume the section/pair separator */
            }
            if (query && *q == '#') {
                ctx->url.cur   = q;  /* query done, fragment begins */
                ctx->url.phase = URL_P_FRAGMENT;
                continue;
            }
            if (*q == '\0') {
                ctx->url.cur   = q;
                ctx->url.phase = URL_P_DONE;
                continue;
            }

            const char *key = q;
            while (*q && *q != '=' && *q != '&' && *q != '#') {
                q++;
            }
            size_t      klen = (size_t)(q - key);
            const char *val  = NULL;
            size_t      vlen = 0;
            if (*q == '=') {
                val = ++q;
                while (*q && *q != '&' && *q != '#') {
                    q++;
                }
                vlen = (size_t)(q - val);
            }

            ctx->url.cur = q;    /* now at '&', '#', or '\0'; phase unchanged */
            if (klen == 0) {
                ctx->error = true;   /* "&&", "?=v" … pair with no key */
                continue;
            }
            url_token(ctx, query ? GECND_URL_KIND_PARAM : GECND_URL_KIND_FRAGMENT,
                      key, klen);
            ctx->url.val.ptr = (void *)val;
            ctx->url.val.len = vlen;
            return true;
        }

        case URL_P_DONE:
        default:
            return false;
        }
    }
}
