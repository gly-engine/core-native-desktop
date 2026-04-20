#pragma once
#include <stddef.h>

/* Stateless URI utilities — all functions are thread-safe.
 *
 * write semantics: if buf==NULL or len==0, returns the source length without
 * writing anything (useful for measuring before allocating).
 *
 * Examples:
 *   "http://example.com/img.png?v=1"  → schema="http", host="example.com",
 *                                        path="/img.png", query="v=1"
 *   "db://blob/data?id=2"             → schema="db",   host="blob",
 *                                        path="/data",   query="id=2"
 *   "textures/hud.png"               → schema="",     host="",
 *                                        path="textures/hud.png"
 */

/* Extracts the schema ("http", "db", "file", "" when absent). */
size_t      gly_uri_schema   (const char *uri, char *buf, size_t len);

/* Extracts the host/authority ("example.com", "blob" in "db://blob/…").
 * Returns "" when there is no "://" in the URI. */
size_t      gly_uri_host     (const char *uri, char *buf, size_t len);

/* Extracts the path ("/img.png", "/data" in "db://blob/data?…").
 * For bare paths without a schema returns the full string up to '?'. */
size_t      gly_uri_path     (const char *uri, char *buf, size_t len);

/* Returns a pointer inside uri to the start of the query string (after '?'),
 * or NULL if there is no query string. */
const char *gly_uri_query    (const char *uri);

/* Looks up a single key in the query string.
 * Returns bytes written (excluding '\0'), or -1 if the key is absent. */
int         gly_uri_query_get(const char *uri, const char *key,
                               char *buf, size_t len);

/* Iterates over every key=value pair in the query string. */
typedef void (*gly_uri_query_cb)(const char *key, size_t klen,
                                  const char *val, size_t vlen,
                                  void       *usr);
void gly_uri_query_each(const char *uri, gly_uri_query_cb cb, void *usr);

/* Maps an HTTP Content-Type header value to an image format hint string.
 * "image/png" → "png", "image/x-etc1" → "etc1", unknown → NULL. */
const char *gly_http_content_type_hint(const char *content_type);
