#include <string.h>
#include <stdlib.h>

#include "gecnd.h"

typedef struct {
    const char *key;
    void       *value;
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

int gecnd_registry(const char *cmd, const char *key, void *const value, void *const usr) {
    if (strcmp(cmd, "set") == 0) {
        size_t pos = lower_bound(key, strlen(key) + 1);
        if (pos < count && strcmp(entries[pos].key, key) == 0) {
            entries[pos].value = value;
            return 0;
        }
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            entries = realloc(entries, capacity * sizeof(*entries));
        }
        memmove(&entries[pos + 1], &entries[pos], (count - pos) * sizeof(*entries));
        entries[pos].key = key;
        entries[pos].value = value;
        count++;
        return 0;
    }

    if (strcmp(cmd, "get") == 0) {
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
