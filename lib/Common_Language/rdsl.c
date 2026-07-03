/**
 * @file lib/Common_Language/rdsl.c
 *
 * @brief Registry DSL: parses keys like "media_player:libretro+$l$0" and,
 *        optionally, matches them against a concrete text in lockstep.
 *
 * @startebnf
 * registry = keyword, { ( ":", keyword ), { ( "+" | "" ), ( keyword | type ) } };
 * keyword = "[a-zA-Z0-9_-]+";
 * type = "$", ( "0" | "s" | "l" | "b"
 *             | ( ( "u" | "i" ), ( "8" | "16" | "32" | "64" ) )
 *             | ( "f", ( "32" | "64" ) ) );
 * @endebnf
 *
 * Type tags (see RDSL_SPECS), dual meaning by context:
 *   $l  literal-string (match: one [A-Za-z0-9_-]+ token) / lua-function (ffi, future)
 *   $s  greedy string (match: consumes the rest) / string param (ffi)
 *   $0  finish-schema (match: rest must contain "://", captures after it) / void (ffi)
 *   $b $u8.. $i8.. $f32 $f64  scalar params (ffi); width-validated on match
 *
 * Match rules (text != NULL):
 *   - every token must match in order and the whole text must be consumed;
 *   - greedy tags ($s, $0) consume the rest, so nothing may follow them;
 *   - kind is the string variant with text, the void variant without it;
 *   - score ranks specificity: literal > number > $l > $0 > $s.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

#define RDSL_SCORE_LITERAL 4
#define RDSL_SCORE_NUMBER  3
#define RDSL_SCORE_WORD    2
#define RDSL_SCORE_FINISH  1
#define RDSL_SCORE_STRING  0

typedef bool (*rdsl_match_fn)(const char *s, size_t n, gecnd_type_t kind, gly_any_t *out);

typedef struct {
    char          tag[4];
    uint8_t       score;
    bool          greedy;
    gecnd_type_t  kind_text;
    gecnd_type_t  kind_void;
    rdsl_match_fn match;
} rdsl_spec_t;

static bool rdsl_word(const char *s, size_t n, gecnd_type_t kind, gly_any_t *out) {
    (void)kind;
    if (n == 0) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    out->ptr = (void *)s;
    out->len = n;
    return true;
}

static bool rdsl_any(const char *s, size_t n, gecnd_type_t kind, gly_any_t *out) {
    (void)kind;
    out->ptr = (void *)s;
    out->len = n;
    return true;
}

static bool rdsl_finish(const char *s, size_t n, gecnd_type_t kind, gly_any_t *out) {
    (void)kind;
    for (size_t i = 0; i + 2 < n; i++) {
        if (s[i] == ':' && s[i + 1] == '/' && s[i + 2] == '/') {
            out->ptr = (void *)(s + i + 3);
            out->len = n - (i + 3);
            return true;
        }
    }
    return false;
}

static bool rdsl_bool(const char *s, size_t n, gecnd_type_t kind, gly_any_t *out) {
    (void)kind;
    if (n == 1 && (s[0] == '0' || s[0] == '1')) {
        out->u8 = (uint8_t)(s[0] - '0');
        return true;
    }
    if (n == 4 && strncmp(s, "true", 4) == 0) {
        out->u8 = 1;
        return true;
    }
    if (n == 5 && strncmp(s, "false", 5) == 0) {
        out->u8 = 0;
        return true;
    }
    return false;
}

static bool rdsl_num(const char *s, size_t n, gecnd_type_t kind, gly_any_t *out) {
    char  buf[24];
    char *end = NULL;
    if (n == 0 || n >= sizeof(buf)) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = s[i];
    }
    buf[n] = '\0';

    if (kind == GECND_TYPE_F32 || kind == GECND_TYPE_F64) {
        double d = strtod(buf, &end);
        if (*end != '\0') {
            return false;
        }
        if (kind == GECND_TYPE_F32) {
            out->f32 = (float)d;
        } else {
            out->f64 = d;
        }
        return true;
    }

    if (kind == GECND_TYPE_U8 || kind == GECND_TYPE_U16
     || kind == GECND_TYPE_U32 || kind == GECND_TYPE_U64) {
        unsigned long long v = strtoull(buf, &end, 10);
        if (*end != '\0') {
            return false;
        }
        switch (kind) {
            case GECND_TYPE_U8:  if (v > 0xFFULL)       return false; out->u8  = (uint8_t)v;  break;
            case GECND_TYPE_U16: if (v > 0xFFFFULL)     return false; out->u16 = (uint16_t)v; break;
            case GECND_TYPE_U32: if (v > 0xFFFFFFFFULL) return false; out->u32 = (uint32_t)v; break;
            default:             out->u64 = (uint64_t)v; break;
        }
        return true;
    }

    long long v = strtoll(buf, &end, 10);
    if (*end != '\0') {
        return false;
    }
    switch (kind) {
        case GECND_TYPE_I8:  if (v < -128LL || v > 127LL)               return false; out->i8  = (int8_t)v;  break;
        case GECND_TYPE_I16: if (v < -32768LL || v > 32767LL)           return false; out->i16 = (int16_t)v; break;
        case GECND_TYPE_I32: if (v < -2147483648LL || v > 2147483647LL) return false; out->i32 = (int32_t)v; break;
        default:             out->i64 = (int64_t)v; break;
    }
    return true;
}

/** @pre entries must stay ordered by `tag` for bsearch(). */
static const rdsl_spec_t RDSL_SPECS[] = {
    { "0",   RDSL_SCORE_FINISH, true,  GECND_TYPE_STRING,  GECND_TYPE_VOID,    rdsl_finish },
    { "b",   RDSL_SCORE_NUMBER, false, GECND_TYPE_BOOLEAN, GECND_TYPE_BOOLEAN, rdsl_bool   },
    { "f32", RDSL_SCORE_NUMBER, false, GECND_TYPE_F32,     GECND_TYPE_F32,     rdsl_num    },
    { "f64", RDSL_SCORE_NUMBER, false, GECND_TYPE_F64,     GECND_TYPE_F64,     rdsl_num    },
    { "i16", RDSL_SCORE_NUMBER, false, GECND_TYPE_I16,     GECND_TYPE_I16,     rdsl_num    },
    { "i32", RDSL_SCORE_NUMBER, false, GECND_TYPE_I32,     GECND_TYPE_I32,     rdsl_num    },
    { "i64", RDSL_SCORE_NUMBER, false, GECND_TYPE_I64,     GECND_TYPE_I64,     rdsl_num    },
    { "i8",  RDSL_SCORE_NUMBER, false, GECND_TYPE_I8,      GECND_TYPE_I8,      rdsl_num    },
    { "l",   RDSL_SCORE_WORD,   false, GECND_TYPE_STRING,  GECND_TYPE_VOID,    rdsl_word   },
    { "s",   RDSL_SCORE_STRING, true,  GECND_TYPE_STRING,  GECND_TYPE_STRING,  rdsl_any    },
    { "u16", RDSL_SCORE_NUMBER, false, GECND_TYPE_U16,     GECND_TYPE_U16,     rdsl_num    },
    { "u32", RDSL_SCORE_NUMBER, false, GECND_TYPE_U32,     GECND_TYPE_U32,     rdsl_num    },
    { "u64", RDSL_SCORE_NUMBER, false, GECND_TYPE_U64,     GECND_TYPE_U64,     rdsl_num    },
    { "u8",  RDSL_SCORE_NUMBER, false, GECND_TYPE_U8,      GECND_TYPE_U8,      rdsl_num    },
};

static int rdsl_spec_cmp(const void *key, const void *el) {
    return strcmp((const char *)key, ((const rdsl_spec_t *)el)->tag);
}

static const rdsl_spec_t *rdsl_spec_find(const char *tag, size_t n) {
    char buf[4];
    if (n == 0 || n >= sizeof(buf)) {
        return NULL;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = tag[i];
    }
    buf[n] = '\0';
    return bsearch(buf, RDSL_SPECS, sizeof(RDSL_SPECS) / sizeof(RDSL_SPECS[0]),
                   sizeof(RDSL_SPECS[0]), rdsl_spec_cmp);
}

static void rdsl_text_token(gecnd_lang_t *const ctx, const char **start, size_t *len) {
    const char *q = ctx->rdsl.tptr;
    while (*q == ':' || *q == '+') {
        q++;
    }
    const char *s = q;
    while (*q != '\0' && *q != ':' && *q != '+' && *q != ')') {
        q++;
    }
    *start = s;
    *len = (size_t)(q - s);
    ctx->rdsl.tptr = q;
}

static void rdsl_text_rest(gecnd_lang_t *const ctx, const char **start, size_t *len) {
    const char *q = ctx->rdsl.tptr;
    if (*q == '+') {
        q++;
    }
    const char *s = q;
    while (*q != '\0' && *q != ')') {
        q++;
    }
    *start = s;
    *len = (size_t)(q - s);
    ctx->rdsl.tptr = q;
}

bool gecnd_lang_rdsl_iterator(gecnd_lang_t *const ctx) {
    const char *pattern = ctx->pattern;
    const char *text    = ctx->text;
    const char *p;
    size_t      n;

    if (ctx->rdsl.ptr == NULL) {
        p = pattern;
        ctx->rdsl.keyidx  = 0;
        ctx->rdsl.plusidx = -1;
        ctx->rdsl.typeidx = -1;
        ctx->rdsl.tptr    = text;
    } else {
        p = ctx->rdsl.ptr + ctx->rdsl.len;
    }

    if (*p == '\0') {
        if (text && ctx->rdsl.tptr && *ctx->rdsl.tptr != '\0' && *ctx->rdsl.tptr != ')') {
            ctx->error = true;
        }
        return false;
    }

    if (*p == ':') {
        p++;
        ctx->rdsl.keyidx++;
    }
    if (*p == '+') {
        ctx->rdsl.plusidx++;
        ctx->rdsl.typeidx = -1;
        p++;
    }

    n = (*p == '$') ? 1 : 0;
    while (p[n] != '\0' && p[n] != ':' && p[n] != '+' && p[n] != '$') {
        n++;
    }

    ctx->rdsl.ptr = p;
    ctx->rdsl.len = (int8_t)n;
    ctx->error    = false;

    if (*p != '$') {
        ctx->rdsl.plusidx = -1;
        ctx->rdsl.typeidx = -1;
        ctx->rdsl.kind    = GECND_TYPE_VOID;
        ctx->rdsl.score   = RDSL_SCORE_LITERAL;
        if (text) {
            const char *ts;
            size_t      tn;
            rdsl_text_token(ctx, &ts, &tn);
            if (tn != n || strncmp(ts, p, n) != 0) {
                ctx->error = true;
                return false;
            }
            ctx->rdsl.val.ptr = (void *)ts;
            ctx->rdsl.val.len = tn;
        }
        return true;
    }

    ctx->rdsl.typeidx++;

    const rdsl_spec_t *spec = rdsl_spec_find(p + 1, n - 1);
    if (!spec) {
        ctx->error     = true;
        ctx->rdsl.kind = GECND_TYPE_VOID;
        return text ? false : true;
    }

    ctx->rdsl.kind  = text ? spec->kind_text : spec->kind_void;
    ctx->rdsl.score = spec->score;

    if (text) {
        const char *ts;
        size_t      tn;
        if (spec->greedy) {
            if (p[n] != '\0') {
                ctx->error = true;
                return false;
            }
            rdsl_text_rest(ctx, &ts, &tn);
        } else {
            rdsl_text_token(ctx, &ts, &tn);
        }
        if (!spec->match(ts, tn, ctx->rdsl.kind, &ctx->rdsl.val)) {
            ctx->error = true;
            return false;
        }
    }

    return true;
}
