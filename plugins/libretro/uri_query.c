#include <string.h>
#include "uri_query.h"

#define URI_QUERY_MAX 32

static struct {
    char key[64];
    char value[256];
} entries[URI_QUERY_MAX];

static int entry_count = 0;

void uri_query_clear(void) {
    entry_count = 0;
}

void uri_query_parse(const char *query) {
    uri_query_clear();
    if (!query || !*query) return;

    char buf[4096];
    strncpy(buf, query, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *pair = buf;
    while (pair && entry_count < URI_QUERY_MAX) {
        char *next = strchr(pair, '&');
        if (next) *next++ = '\0';

        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            strncpy(entries[entry_count].key,   pair,    sizeof(entries[0].key)   - 1);
            strncpy(entries[entry_count].value,  eq + 1,  sizeof(entries[0].value) - 1);
            entries[entry_count].key  [sizeof(entries[0].key)   - 1] = '\0';
            entries[entry_count].value[sizeof(entries[0].value) - 1] = '\0';
            entry_count++;
        }

        pair = next;
    }
}

const char *uri_query_get(const char *key) {
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].key, key) == 0)
            return entries[i].value;
    }
    return NULL;
}
