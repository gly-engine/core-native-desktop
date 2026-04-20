#include <string.h>
#include <stddef.h>
#include "uri.h"

static size_t write_buf(char *buf, size_t buf_len, const char *src, size_t src_len) {
    if (buf && buf_len) {
        size_t copy = src_len < buf_len - 1 ? src_len : buf_len - 1;
        memcpy(buf, src, copy);
        buf[copy] = '\0';
    }
    return src_len;
}

size_t gly_uri_schema(const char *uri, char *buf, size_t len) {
    if (!uri) return write_buf(buf, len, "", 0);
    const char *sep = strstr(uri, "://");
    if (!sep) return write_buf(buf, len, "", 0);
    return write_buf(buf, len, uri, (size_t)(sep - uri));
}

size_t gly_uri_host(const char *uri, char *buf, size_t len) {
    if (!uri) return write_buf(buf, len, "", 0);
    const char *sep = strstr(uri, "://");
    if (!sep) return write_buf(buf, len, "", 0);
    const char *start = sep + 3;
    const char *end   = strpbrk(start, "/?#");
    if (!end) end = start + strlen(start);
    return write_buf(buf, len, start, (size_t)(end - start));
}

size_t gly_uri_path(const char *uri, char *buf, size_t len) {
    if (!uri) return write_buf(buf, len, "", 0);
    const char *start;
    const char *sep = strstr(uri, "://");
    if (sep) {
        start = sep + 3;
        start = strpbrk(start, "/?#");
        if (!start || *start != '/') return write_buf(buf, len, "", 0);
    } else {
        start = uri;
    }
    const char *end = strpbrk(start, "?#");
    if (!end) end = start + strlen(start);
    return write_buf(buf, len, start, (size_t)(end - start));
}

const char *gly_uri_query(const char *uri) {
    if (!uri) return NULL;
    const char *q = strchr(uri, '?');
    return q ? q + 1 : NULL;
}

int gly_uri_query_get(const char *uri, const char *key, char *buf, size_t len) {
    const char *q = gly_uri_query(uri);
    if (!q || !key) return -1;
    size_t klen = strlen(key);
    const char *p = q;
    while (*p) {
        const char *eq   = strchr(p, '=');
        if (!eq) break;
        const char *vend = strpbrk(eq + 1, "&#");
        if (!vend) vend  = eq + 1 + strlen(eq + 1);
        if ((size_t)(eq - p) == klen && memcmp(p, key, klen) == 0) {
            return (int)write_buf(buf, len, eq + 1, (size_t)(vend - eq - 1));
        }
        if (*vend == '\0' || *vend == '#') break;
        p = vend + 1;
    }
    return -1;
}

void gly_uri_query_each(const char *uri, gly_uri_query_cb cb, void *usr) {
    const char *q = gly_uri_query(uri);
    if (!q || !cb) return;
    const char *p = q;
    while (*p) {
        const char *eq   = strchr(p, '=');
        if (!eq) break;
        const char *vend = strpbrk(eq + 1, "&#");
        if (!vend) vend  = eq + 1 + strlen(eq + 1);
        cb(p, (size_t)(eq - p), eq + 1, (size_t)(vend - eq - 1), usr);
        if (*vend == '\0' || *vend == '#') break;
        p = vend + 1;
    }
}

static const struct { const char *ct; const char *hint; } s_ct_map[] = {
    { "image/png",     "png"  },
    { "image/jpeg",    "jpg"  },
    { "image/jpg",     "jpg"  },
    { "image/webp",    "webp" },
    { "image/x-etc1", "etc1" },
    { "image/x-etc2", "etc2" },
    { NULL, NULL }
};

const char *gly_http_content_type_hint(const char *content_type) {
    if (!content_type) return NULL;
    for (int i = 0; s_ct_map[i].ct; i++) {
        if (strncmp(content_type, s_ct_map[i].ct, strlen(s_ct_map[i].ct)) == 0)
            return s_ct_map[i].hint;
    }
    return NULL;
}
