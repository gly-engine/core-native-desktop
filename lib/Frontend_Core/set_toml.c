#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <tomlc17.h>
#include <ketopt.h>

#include "gecnd.h"


void gecnd_set_opt(gecnd_t *gly, int c, ketopt_t opt);

/* recursively traverse [keymap] tables; path built dot-by-dot */
static void traverse_keymap(toml_datum_t node, const char *path)
{
    if (node.type == TOML_TABLE) {
        /* check if all values are arrays (leaf table = keymap class) */
        int all_arrays = 1;
        for (int i = 0; i < node.u.tab.size; i++) {
            if (node.u.tab.value[i].type != TOML_ARRAY) {
                all_arrays = 0;
                break;
            }
        }

        if (all_arrays && node.u.tab.size > 0) {
            /* leaf — register as class */
            for (int i = 0; i < node.u.tab.size; i++) {
                const char *key_name = node.u.tab.key[i];
                toml_datum_t arr     = node.u.tab.value[i];
                for (int j = 0; j < arr.u.arr.size; j++) {
                    toml_datum_t elem = arr.u.arr.elem[j];
                    if (elem.type == TOML_INT64)
                        gamely_daemon_input_add_keycode(path, key_name, (uint32_t)elem.u.int64);
                }
            }
        } else {
            /* inner table — recurse */
            for (int i = 0; i < node.u.tab.size; i++) {
                const char *name = node.u.tab.key[i];
                char child[strlen(path) + strlen(name) + 2];
                snprintf(child, sizeof(child), path[0] ? "%s.%s" : "%s%s", path, name);
                traverse_keymap(node.u.tab.value[i], child);
            }
        }
    }
}

typedef struct {
    gecnd_t      *gly;
    const char   *toml_path;
    toml_datum_t  args;
    unsigned      busy;     /* SRC_* já sendo expandidos (anti-loop) */
} expand_ctx_t;

enum { SRC_GAME = 1, SRC_ENGINE = 2 };

static size_t expand_placeholders(const expand_ctx_t *ctx, const char *value, char *out);

static void emit(char *out, size_t *pos, const char *s, size_t n)
{
    if (out) memcpy(out + *pos, s, n);
    *pos += n;
}

/* emite os n primeiros bytes de `path`; relativo vira absoluto via {cwd}.
 * URL (http/https) fica como está. */
static void emit_path(char *out, size_t *pos, const char *path, size_t n)
{
    bool is_abs = path[0] == '/' || strstr(path, "://") != NULL
#if defined(_WIN32)
        || path[0] == '\\' || (path[0] && path[1] == ':')
#endif
        ;
    if (!is_abs) {
        const char *cwd = NULL;
        gecnd_registry("get", "cwd", &cwd, NULL);
        if (cwd && cwd[0]) {
            emit(out, pos, cwd, strlen(cwd));
            if (n > 0) emit(out, pos, "/", 1);
        }
    }
    emit(out, pos, path, n);
}

/* diretório de `path`, sem a barra final */
static void emit_dirname(char *out, size_t *pos, const char *path)
{
    if (!path || !path[0]) return;
    const char *sep = strrchr(path, '/');
#if defined(_WIN32)
    const char *bsep = strrchr(path, '\\');
    if (bsep > sep) sep = bsep;
#endif
    emit_path(out, pos, path, !sep ? 0 : sep == path ? 1 : (size_t)(sep - path));
}

/* diretório do game/engine: o [args] deste toml ganha (é aplicado logo
 * depois do [envs]), senão o que já veio da linha de comando. O valor do
 * [args] também é expandido; `busy` corta loop tipo game = "{cwd:game}/x". */
static void emit_source_dir(const expand_ctx_t *ctx, char *out, size_t *pos,
                            const char *opt, unsigned src, const char *uri)
{
    toml_datum_t v = {0};
    bool whole = false;
    if (ctx->args.type == TOML_TABLE && !(ctx->busy & src)) {
        v = toml_get(ctx->args, opt);
        if (v.type != TOML_STRING) {
            v = toml_get(ctx->args, "game+engine");
            whole = true;
        }
    }
    if (v.type != TOML_STRING) { emit_dirname(out, pos, uri); return; }

    expand_ctx_t inner = *ctx;
    inner.busy |= whole ? (SRC_GAME | SRC_ENGINE) : src;
    char str[expand_placeholders(&inner, v.u.str.ptr, NULL) + 1];
    expand_placeholders(&inner, v.u.str.ptr, str);
    if (!whole) { emit_dirname(out, pos, str); return; }

    size_t n = strlen(str);
    while (n > 1 && str[n - 1] == '/') n--;
    emit_path(out, pos, str, n);
}

/* troca os placeholders de `value`:
 *   {cwd}         diretório de trabalho do processo
 *   {cwd:core}    diretório do executável
 *   {cwd:game}    diretório do --game   (se já conhecido)
 *   {cwd:engine}  diretório do --engine (se já conhecido)
 *   {cwd:toml}    diretório deste .toml
 *   {env:NOME}    variável de ambiente (vazio se não existir)
 * Vale pra toda string do toml: [envs], [registry] e [args].
 * Placeholder desconhecido fica literal. Com out == NULL só mede;
 * retorna o tamanho sem o '\0'. */
static size_t expand_placeholders(const expand_ctx_t *ctx, const char *value, char *out)
{
    size_t pos = 0;
    for (const char *p = value; *p; ) {
        const char *end = *p == '{' ? strchr(p, '}') : NULL;
        if (!end) { emit(out, &pos, p++, 1); continue; }

        const char *tok = p + 1;
        size_t      len = (size_t)(end - tok);
        const char *reg = NULL;
#define IS(lit) (len == sizeof(lit) - 1 && strncmp(tok, lit, len) == 0)
        if (IS("cwd")) {
            gecnd_registry("get", "cwd", &reg, NULL);
            if (reg) emit(out, &pos, reg, strlen(reg));
        } else if (IS("cwd:core")) {
            gecnd_registry("get", "pwd", &reg, NULL);
            if (reg) emit(out, &pos, reg, strlen(reg));
        } else if (IS("cwd:game")) {
            emit_source_dir(ctx, out, &pos, "game", SRC_GAME, ctx->gly->game_source.uri);
        } else if (IS("cwd:engine")) {
            emit_source_dir(ctx, out, &pos, "engine", SRC_ENGINE, ctx->gly->engine_source.uri);
        } else if (IS("cwd:toml")) {
            emit_dirname(out, &pos, ctx->toml_path);
        } else if (len > 4 && strncmp(tok, "env:", 4) == 0) {
            char name[len - 3];
            memcpy(name, tok + 4, len - 4);
            name[len - 4] = '\0';
            const char *env = getenv(name);
            if (env) emit(out, &pos, env, strlen(env));
        } else {
            emit(out, &pos, p++, 1);
            continue;
        }
#undef IS
        p = end + 1;
    }
    if (out) out[pos] = '\0';
    return pos;
}

static char *expand_strdup(const expand_ctx_t *ctx, const char *value)
{
    char *out = malloc(expand_placeholders(ctx, value, NULL) + 1);
    if (out) expand_placeholders(ctx, value, out);
    return out;
}


/* [registry] — cada chave vira registry("set", ...); tabelas aninhadas
 * juntam com ':' (ex.: [registry.web_http_path] rc="./rc2" → "web_http_path:rc").
 * O TOML é liberado no fim do parse, então key (e value string) são strdup
 * pelo próprio registry via opções de armazenamento. Strings passam
 * pelos placeholders (ver expand_placeholders). */
static void traverse_registry(const expand_ctx_t *ctx, toml_datum_t node, const char *prefix)
{
    if (node.type != TOML_TABLE) return;
    for (int i = 0; i < node.u.tab.size; i++) {
        const char *name = node.u.tab.key[i];
        char key[strlen(prefix) + strlen(name) + 2];
        snprintf(key, sizeof(key), prefix[0] ? "%s:%s" : "%s%s", prefix, name);

        toml_datum_t val = node.u.tab.value[i];
        switch (val.type) {
            case TOML_TABLE:
                traverse_registry(ctx, val, key);
                break;
            case TOML_INT64:
                gecnd_registry("set", key, (void *)(intptr_t)val.u.int64, "strdup=key");
                break;
            case TOML_BOOLEAN:
                gecnd_registry("set", key, (void *)(intptr_t)(val.u.boolean ? 1 : 0), "strdup=key");
                break;
            case TOML_STRING: {
                char str[expand_placeholders(ctx, val.u.str.ptr, NULL) + 1];
                expand_placeholders(ctx, val.u.str.ptr, str);
                gecnd_registry("set", key, (void *)str, "strdup=keyval");
                break;
            }
            default:
                fprintf(stderr, "[core:toml] incompatible type for registry key '%s'\n", key);
                break;
        }
    }
}

void gamely_set_toml(gecnd_t *gly, const char *path, ko_longopt_t *longopts)
{
    if (!gly || !path) return;

    toml_result_t res = toml_parse_file_ex(path);
    if (!res.ok) {
        fprintf(stderr, "[core:toml] parse error: %s\n", res.errmsg);
        toml_free(res);
        return;
    }

    expand_ctx_t ctx = {
        .gly       = gly,
        .toml_path = path,
        .args      = toml_get(res.toptab, "args"),
    };
    toml_datum_t envs = toml_get(res.toptab, "envs");
    if (envs.type == TOML_TABLE) {
        for (int i = 0; i < envs.u.tab.size; i++) {
            toml_datum_t val = envs.u.tab.value[i];
            if (val.type != TOML_STRING) {
                gecnd_add_error(gly, "[core:toml] env '%s' must be a string",
                                envs.u.tab.key[i]);
                continue;
            }
            char expanded[expand_placeholders(&ctx, val.u.str.ptr, NULL) + 1];
            expand_placeholders(&ctx, val.u.str.ptr, expanded);
#if defined(_WIN32)
            SetEnvironmentVariable(envs.u.tab.key[i], expanded);
#else
            setenv(envs.u.tab.key[i], expanded, 1);
#endif
        }
    }

    /* apply [registry] */
    toml_datum_t reg = toml_get(res.toptab, "registry");
    if (reg.type == TOML_TABLE) traverse_registry(&ctx, reg, "");

    /* traverse [keymap] */
    toml_datum_t keymap = toml_get(res.toptab, "keymap");
    if (keymap.type == TOML_TABLE) traverse_keymap(keymap, "");

    /* apply [args] — stop before longopts last named entry (toml/9999) to prevent recursion */
    toml_datum_t args = ctx.args;
    if (args.type == TOML_TABLE) {
        for (int i = 0; longopts[i].name != NULL && longopts[i + 1].name != NULL; i++) {
            toml_datum_t val = toml_get(args, longopts[i].name);
            ketopt_t fake = {0};

            if (longopts[i].has_arg == ko_no_argument) {
                if (val.type == TOML_BOOLEAN && val.u.boolean)
                    gecnd_set_opt(gly, longopts[i].val, fake);
            } else if (val.type == TOML_STRING) {
                fake.arg = expand_strdup(&ctx, val.u.str.ptr); /** @todo memory leak here*/
                gecnd_set_opt(gly, longopts[i].val, fake);
            } else if (val.type == TOML_INT64) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)val.u.int64);
                fake.arg = strdup(buf);
                gecnd_set_opt(gly, longopts[i].val, fake);
            } else if (val.type == TOML_FP64) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", val.u.fp64);
                fake.arg = strdup(buf);
                gecnd_set_opt(gly, longopts[i].val, fake);
            } else if (val.type == TOML_ARRAY) {
                /* array requires plural key (e.g. "plugins" not "plugin") */
            } else if (val.type != 0) {
                fprintf(stderr, "[core:toml] incompatible type for key '%s'\n",
                        longopts[i].name);
            } else {
                char plural[strlen(longopts[i].name) + 2];
                snprintf(plural, sizeof(plural), "%ss", longopts[i].name);
                toml_datum_t arr = toml_get(args, plural);
                if (arr.type == TOML_ARRAY) {
                    for (int j = 0; j < arr.u.arr.size; j++) {
                        toml_datum_t elem = arr.u.arr.elem[j];
                        if (elem.type == TOML_STRING) {
                            fake.arg = expand_strdup(&ctx, elem.u.str.ptr); /** @todo memory leak here*/
                            gecnd_set_opt(gly, longopts[i].val, fake);
                        }
                    }
                } else if (arr.type != 0) {
                    /* TODO: error — plural key must be an array */
                }
            }
        }
    }

    toml_free(res);
}
