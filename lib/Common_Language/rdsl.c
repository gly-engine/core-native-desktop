/**
 * @file lib/Common_Language/rdsl.c
 *
 * @brief
 * @startebnf
 * registry = keyword, { ( ":", keyword ),  { "+", { type }- } };
 * keyword = "[a-z_]+";
 * type = "$", ("0" | "s" | "b" | (("u" | "i"), ("8" | "16" | "32" |"64")) |  "f", ("32" | "64"));
 * @endebnf
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum __attribute__((packed)) {
    GECND_TYPE_VOID,
    GECND_TYPE_STRING,
    GECND_TYPE_BOOLEAN,
    GECND_TYPE_U8,
    GECND_TYPE_U16,
    GECND_TYPE_U32,
    GECND_TYPE_U64,
    GECND_TYPE_I8,
    GECND_TYPE_I16,
    GECND_TYPE_I32,
    GECND_TYPE_I64,
    GECND_TYPE_F32,
    GECND_TYPE_F64,
} gecnd_type_t;

typedef struct {
    const char *ptr;
    bool error;
    int8_t len;
    int8_t keyidx;
    int8_t plusidx;
    int8_t typeidx;
    gecnd_type_t kind;
} gecnd_lang_rdsl_t;

/**
 * @param[in,out] ctx
 * @param[in] text
 */
bool gecnd_lang_rdsl_iterator(gecnd_lang_rdsl_t *const ctx, const char *text) {
    const char *p;
    size_t n;
    int w;

    if (ctx->ptr == NULL) {
        p = text;
        ctx->keyidx = -1;
        ctx->plusidx = -1;
        ctx->typeidx = -1;
    } else {
        p = ctx->ptr + ctx->len;
    }

    if (*p == '\0') {
        return false;
    }

    if (*p == ':') {
        p++;
    }

    if (*p == '+') {
        ctx->plusidx++;
        ctx->typeidx = -1;
        p++;
    }

    n = (*p == '$') ? 1 : 0;
    while (p[n] != '\0' && p[n] != ':' && p[n] != '+' && p[n] != '$') {
        n++;
    }

    ctx->ptr = p;
    ctx->len = n;
    ctx->error = false;

    if (*p != '$') {
        ctx->keyidx++;
        ctx->plusidx = -1;
        ctx->typeidx = -1;
        ctx->kind = GECND_TYPE_VOID;
        return true;
    }

    ctx->typeidx++;
    w = (n > 2) ? atoi(p + 2) : 0;

    switch (p[1]) {
        case '0': ctx->kind = GECND_TYPE_VOID;    break;
        case 's': ctx->kind = GECND_TYPE_STRING;  break;
        case 'b': ctx->kind = GECND_TYPE_BOOLEAN; break;
        case 'u':
            switch (w) {
                case 8:  ctx->kind = GECND_TYPE_U8;  break;
                case 16: ctx->kind = GECND_TYPE_U16; break;
                case 32: ctx->kind = GECND_TYPE_U32; break;
                case 64: ctx->kind = GECND_TYPE_U64; break;
                default: ctx->error = true;          break;
            }
            break;
        case 'i':
            switch (w) {
                case 8:  ctx->kind = GECND_TYPE_I8;  break;
                case 16: ctx->kind = GECND_TYPE_I16; break;
                case 32: ctx->kind = GECND_TYPE_I32; break;
                case 64: ctx->kind = GECND_TYPE_I64; break;
                default: ctx->error = true;          break;
            }
            break;
        case 'f':
            switch (w) {
                case 32: ctx->kind = GECND_TYPE_F32; break;
                case 64: ctx->kind = GECND_TYPE_F64; break;
                default: ctx->error = true;          break;
            }
            break;
        default:
            ctx->error = true;
            break;
    }

    return true;
}