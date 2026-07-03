#include <string.h>
#include <stdlib.h>

#include "gecnd.h"

typedef struct hook_node {
    gecnd_registry_handler handler;
    void                  *usr;
    struct hook_node      *next;
} hook_node_t;

typedef struct {
    const char  *key;
    void        *value;
    hook_node_t *hooks;
} gecnd_registry_entry_t;

static gecnd_registry_entry_t *entries;
static size_t count;
static size_t capacity;

static size_t lower_bound(const char *key, size_t len) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strncmp(entries[mid].key, key, len) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static size_t entry_intern(const char *key) {
    size_t pos = lower_bound(key, strlen(key) + 1);
    if (pos < count && strcmp(entries[pos].key, key) == 0) return pos;
    if (count == capacity) {
        capacity = capacity ? capacity * 2 : 16;
        entries = realloc(entries, capacity * sizeof(*entries));
    }
    memmove(&entries[pos + 1], &entries[pos], (count - pos) * sizeof(*entries));
    entries[pos].key   = key;
    entries[pos].value = NULL;
    entries[pos].hooks = NULL;
    count++;
    return pos;
}

int gecnd_registry(const char *cmd, const char *key, void *const value, void *const usr) {
    if (strcmp(cmd, "set") == 0) {
        size_t pos = entry_intern(key);
        entries[pos].value = value;
        for (hook_node_t *h = entries[pos].hooks; h; h = h->next) {
            h->handler(key, value, h->usr);
        }
        return 0;
    }

    if (strcmp(cmd, "hook") == 0) {
        size_t pos = entry_intern(key);
        hook_node_t *node = malloc(sizeof(*node));
        if (!node) return -1;
        node->handler = (gecnd_registry_handler)value;
        node->usr     = usr;
        node->next    = entries[pos].hooks;
        entries[pos].hooks = node;
        if (entries[pos].value) node->handler(key, entries[pos].value, usr);
        return 0;
    }

    if (strcmp(cmd, "get") == 0) {
        const char *paren = strchr(key, '(');
        if (paren) {
            size_t                 plen    = (size_t)(paren - key);
            const char            *text    = paren + 1;
            gecnd_registry_handler handler = (gecnd_registry_handler)value;
            const char            *best_key = NULL;
            void                  *best_val = NULL;
            int                    best_score = -1;
            for (size_t i = lower_bound(key, plen); i < count; i++) {
                if (strncmp(entries[i].key, key, plen) != 0) break;
                gecnd_lang_t ctx = {{ "rdsl", entries[i].key + plen, text }};
                int score = 0;
                while (gecnd_lang(&ctx)) {
                    score += ctx.rdsl.score;
                }
                if (ctx.error) continue;
                if (score > best_score) {
                    best_score = score;
                    best_key   = entries[i].key;
                    best_val   = entries[i].value;
                }
            }
            if (handler) handler(best_key, best_val, usr);
            return best_score >= 0 ? 1 : 0;
        }
        const char *star = strchr(key, '*');
        if (!star) {
            size_t pos = lower_bound(key, strlen(key) + 1);
            if (pos >= count || strcmp(entries[pos].key, key) != 0) return 0;
            if (value) *(void **)value = entries[pos].value;
            return 1;
        }
        size_t len = (size_t)(star - key);
        int found = 0;
        for (size_t i = lower_bound(key, len); i < count; i++) {
            if (strncmp(entries[i].key, key, len) != 0) break;
            gecnd_registry_handler handler = value ? (gecnd_registry_handler)value
                                                   : (gecnd_registry_handler)entries[i].value;
            handler(entries[i].key, entries[i].value, usr);
            found++;
        }
        return found;
    }

    return -1;
}
