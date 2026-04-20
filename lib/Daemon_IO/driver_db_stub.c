#include <stdint.h>
#include <stddef.h>
#include "gecnd.h"

void    gamely_daemon_db_start(void)                              {}
void    gamely_daemon_db_stop (void)                              {}

int32_t gamely_daemon_db_insert_media(const char *name, const char *short_id,
                                       const char *url,  const char *type,
                                       const char *url_image) {
    (void)name; (void)short_id; (void)url; (void)type; (void)url_image;
    return -1;
}

void    gamely_daemon_db_delete_media(const char *short_id)       { (void)short_id; }

int32_t gamely_daemon_db_insert_blob(const uint8_t *data, size_t len,
                                      const char *hint) {
    (void)data; (void)len; (void)hint;
    return -1;
}

void    gamely_daemon_db_delete_blob(int32_t id)                  { (void)id; }

void        gamely_daemon_db_kv_set(const char *key, const char *value) { (void)key; (void)value; }
const char *gamely_daemon_db_kv_get(const char *key)              { (void)key; return NULL; }

void       *gamely_daemon_db_query_uri(const char *uri, size_t *out_len) {
    (void)uri; (void)out_len;
    return NULL;
}
