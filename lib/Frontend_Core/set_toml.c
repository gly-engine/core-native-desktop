#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <tomlc17.h>
#include <ketopt.h>

#include "gecnd.h"
#include "gamely_input.h"

void gecnd_set_opt(gecnd_t *gly, int c, ketopt_t opt);

/* recursively traverse [keymap] tables; path built dot-by-dot */
static void traverse_keymap(toml_datum_t node, char *path, int path_len)
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
            gamely_daemon_input_add_class(path);
            for (int i = 0; i < node.u.tab.size; i++) {
                const char *key_name = node.u.tab.key[i];
                toml_datum_t arr     = node.u.tab.value[i];
                for (int j = 0; j < arr.u.arr.size; j++) {
                    toml_datum_t elem = arr.u.arr.elem[j];
                    if (elem.type == TOML_INT64)
                        gamely_daemon_input_add_keycode(key_name, (uint32_t)elem.u.int64);
                }
            }
        } else {
            /* inner table — recurse */
            for (int i = 0; i < node.u.tab.size; i++) {
                char child[128];
                if (path_len > 0)
                    snprintf(child, sizeof(child), "%s.%s", path, node.u.tab.key[i]);
                else
                    snprintf(child, sizeof(child), "%s", node.u.tab.key[i]);
                traverse_keymap(node.u.tab.value[i], child, (int)strlen(child));
            }
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

    /* apply [envs] */
    toml_datum_t envs = toml_get(res.toptab, "envs");
    if (envs.type == TOML_TABLE) {
        for (int i = 0; i < envs.u.tab.size; i++) {
            toml_datum_t val = envs.u.tab.value[i];
            if (val.type == TOML_STRING)
                setenv(envs.u.tab.key[i], val.u.str.ptr, 1);
        }
    }

    /* traverse [keymap] */
    toml_datum_t keymap = toml_get(res.toptab, "keymap");
    if (keymap.type == TOML_TABLE) {
        char path_buf[128] = {0};
        traverse_keymap(keymap, path_buf, 0);
    }

    /* apply [args] — stop before longopts last named entry (toml/9999) to prevent recursion */
    toml_datum_t args = toml_get(res.toptab, "args");
    if (args.type == TOML_TABLE) {
        for (int i = 0; longopts[i].name != NULL && longopts[i + 1].name != NULL; i++) {
            toml_datum_t val = toml_get(args, longopts[i].name);
            ketopt_t fake = {0};

            if (longopts[i].has_arg == ko_no_argument) {
                if (val.type == TOML_BOOLEAN && val.u.boolean)
                    gecnd_set_opt(gly, longopts[i].val, fake);
            } else if (val.type == TOML_STRING) {
                fake.arg = strdup(val.u.str.ptr); /** @todo memory leak here*/
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
                char plural[68];
                snprintf(plural, sizeof(plural), "%ss", longopts[i].name);
                toml_datum_t arr = toml_get(args, plural);
                if (arr.type == TOML_ARRAY) {
                    for (int j = 0; j < arr.u.arr.size; j++) {
                        toml_datum_t elem = arr.u.arr.elem[j];
                        if (elem.type == TOML_STRING) {
                            fake.arg = strdup(elem.u.str.ptr); /** @todo memory leak here*/
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
