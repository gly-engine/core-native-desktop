#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <tomlc17.h>

#include "gecnd.h"
#include "gamely_input.h"
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

void gamely_set_toml(gecnd_t *gly, const char *path)
{
    if (!gly || !path) return;

    toml_result_t res = toml_parse_file_ex(path);
    if (!res.ok) {
        fprintf(stderr, "[core:toml] parse error: %s\n", res.errmsg);
        toml_free(res);
        return;
    }

    /* traverse [keymap] */
    toml_datum_t keymap = toml_get(res.toptab, "keymap");
    if (keymap.type == TOML_TABLE) {
        char path_buf[128] = {0};
        traverse_keymap(keymap, path_buf, 0);
    }

    /* apply [args] as CLI — last value wins; --toml inside TOML is ignored */
    toml_datum_t args = toml_get(res.toptab, "args");
    if (args.type == TOML_TABLE) {
        toml_datum_t input_val = toml_get(args, "input");
        if (input_val.type == TOML_STRING)
            gly->input = strdup(input_val.u.str.ptr); /* known leak; intentional */
    }

    toml_free(res);
}
