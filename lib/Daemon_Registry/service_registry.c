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
    size_t       value;   /* index into bucket; 0 = never set */
    hook_node_t *hooks;
} gecnd_registry_entry_t;

static gecnd_registry_entry_t *entries;
static size_t count;
static size_t capacity;

/* bucket[0] is reserved as the "no value" sentinel, so an entry created by
 * bind/hook before any set keeps index 0 and has/get report it as absent. */
static void **bucket;
static size_t bucket_count;
static size_t bucket_capacity;

#define BIND_HANDLER(suffix, type)                                  \
    static void bind_set_##suffix(const char *k, void *v, void *usr) { \
        (void)k; *(type *)usr = *(type *)v;                         \
    }

static void bind_set_ptr(const char *k, void *v, void *usr) {
    (void)k; *(void **)usr = v;
}

BIND_HANDLER(bool, bool)
BIND_HANDLER(u8,  uint8_t)
BIND_HANDLER(u16, uint16_t)
BIND_HANDLER(u32, uint32_t)
BIND_HANDLER(u64, uint64_t)
BIND_HANDLER(i8,  int8_t)
BIND_HANDLER(i16, int16_t)
BIND_HANDLER(i32, int32_t)
BIND_HANDLER(i64, int64_t)
BIND_HANDLER(f32, float)
BIND_HANDLER(f64, double)
#undef BIND_HANDLER

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
    entries[pos].value = 0;
    entries[pos].hooks = NULL;
    count++;
    return pos;
}

static size_t bucket_intern(void) {
    if (bucket_count + 1 >= bucket_capacity) {
        bucket_capacity = bucket_capacity ? bucket_capacity * 2 : 16;
        bucket = realloc(bucket, bucket_capacity * sizeof(*bucket));
    }
    if (bucket_count == 0) bucket[bucket_count++] = NULL;
    return bucket_count++;
}

int gecnd_registry(const char *cmd, const char *key, void *const value, void *const usr) {
    if (strcmp(cmd, "set") == 0) {
        size_t pos = entry_intern(key);
        if (entries[pos].value == 0) entries[pos].value = bucket_intern();
        bucket[entries[pos].value] = value;
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
        if (entries[pos].value) node->handler(key, bucket[entries[pos].value], usr);
        return 0;
    }

    if (strcmp(cmd, "bind") == 0) {
        gecnd_registry_handler handler;
        switch ((gecnd_type_t)(intptr_t)usr) {
            case GECND_TYPE_VOID:
            case GECND_TYPE_STRING:  handler = bind_set_ptr;  break;
            case GECND_TYPE_BOOLEAN: handler = bind_set_bool; break;
            case GECND_TYPE_U8:      handler = bind_set_u8;   break;
            case GECND_TYPE_U16:     handler = bind_set_u16;  break;
            case GECND_TYPE_U32:     handler = bind_set_u32;  break;
            case GECND_TYPE_U64:     handler = bind_set_u64;  break;
            case GECND_TYPE_I8:      handler = bind_set_i8;   break;
            case GECND_TYPE_I16:     handler = bind_set_i16;  break;
            case GECND_TYPE_I32:     handler = bind_set_i32;  break;
            case GECND_TYPE_I64:     handler = bind_set_i64;  break;
            case GECND_TYPE_F32:     handler = bind_set_f32;  break;
            case GECND_TYPE_F64:     handler = bind_set_f64;  break;
            default:                 return -1;
        }
        return gecnd_registry("hook", key, (void *)handler, value);
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
                if (entries[i].value == 0) continue;
                gecnd_lang_t ctx = {{ "rdsl", entries[i].key + plen, text }};
                int score = 0;
                while (gecnd_lang(&ctx)) {
                    score += ctx.rdsl.score;
                }
                if (ctx.error) continue;
                if (score > best_score) {
                    best_score = score;
                    best_key   = entries[i].key;
                    best_val   = bucket[entries[i].value];
                }
            }
            if (handler) handler(best_key, best_val, usr);
            return best_score >= 0 ? 1 : 0;
        }
        const char *star = strchr(key, '*');
        if (!star) {
            size_t pos = lower_bound(key, strlen(key) + 1);
            if (pos >= count || strcmp(entries[pos].key, key) != 0) return 0;
            if (entries[pos].value == 0) return 0;
            if (value) *(void **)value = bucket[entries[pos].value];
            return 1;
        }
        size_t len = (size_t)(star - key);
        int found = 0;
        for (size_t i = lower_bound(key, len); i < count; i++) {
            if (strncmp(entries[i].key, key, len) != 0) break;
            if (entries[i].value == 0) continue;
            gecnd_registry_handler handler = value ? (gecnd_registry_handler)value
                                                   : (gecnd_registry_handler)bucket[entries[i].value];
            handler(entries[i].key, bucket[entries[i].value], usr);
            found++;
        }
        return found;
    }

    return -1;
}
