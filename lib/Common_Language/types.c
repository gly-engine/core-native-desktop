#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

static const struct type_tag {
    char         tag[4];
    gecnd_type_t kind;
} TYPE_TAGS[] = {
    { "b",   GECND_TYPE_BOOLEAN },
    { "f32", GECND_TYPE_F32 },
    { "f64", GECND_TYPE_F64 },
    { "i16", GECND_TYPE_I16 },
    { "i32", GECND_TYPE_I32 },
    { "i64", GECND_TYPE_I64 },
    { "i8",  GECND_TYPE_I8 },
    { "s",   GECND_TYPE_STRING },
    { "u16", GECND_TYPE_U16 },
    { "u32", GECND_TYPE_U32 },
    { "u64", GECND_TYPE_U64 },
    { "u8",  GECND_TYPE_U8 },
};

static int type_cmp(const void *key, const void *el) {
    return strcmp((const char *)key, ((const struct type_tag *)el)->tag);
}

gecnd_type_t gecnd_lang_type(const char *tag, size_t n) {
    char buf[4];
    if (n == 0 || n >= sizeof(buf)) {
        return GECND_TYPE_VOID;
    }
    memcpy(buf, tag, n);
    buf[n] = '\0';

    const struct type_tag *e = bsearch(buf, TYPE_TAGS,
        sizeof(TYPE_TAGS) / sizeof(*TYPE_TAGS), sizeof(*TYPE_TAGS), type_cmp);
    return e ? e->kind : GECND_TYPE_VOID;
}

bool gecnd_lang_coerce(gecnd_type_t kind, const char *s, size_t n, gly_any_t *out) {
    if (kind == GECND_TYPE_STRING) {
        out->ptr = (void *)s;
        out->len = n;
        return true;
    }

    if (kind == GECND_TYPE_BOOLEAN) {
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

    char  buf[24];
    char *end = NULL;
    if (n == 0 || n >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, s, n);
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

    if (kind == GECND_TYPE_I8 || kind == GECND_TYPE_I16
     || kind == GECND_TYPE_I32 || kind == GECND_TYPE_I64) {
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

    return false;
}
