#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sqlite3.h>

#include "gecnd.h"
#include "../Common_Utils/uri.h"

#define WHERE_MAX  16
#define KV_BUF_CAP 4096

static sqlite3 *s_db    = NULL;
static char     s_kv_buf[KV_BUF_CAP];

/* ── internal helpers ─────────────────────────────────────────────── */

static void exec_sql(const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(s_db, sql, NULL, NULL, &err) != SQLITE_OK && err)
        sqlite3_free(err);
}

typedef struct {
    char keys[WHERE_MAX][64];
    char vals[WHERE_MAX][256];
    int  n;
} where_t;

static void collect_kv(const char *k, size_t kl, const char *v, size_t vl, void *usr) {
    where_t *w = (where_t *)usr;
    if (w->n >= WHERE_MAX) return;
    size_t ki = kl < 63  ? kl : 63;
    size_t vi = vl < 255 ? vl : 255;
    memcpy(w->keys[w->n], k, ki); w->keys[w->n][ki] = '\0';
    memcpy(w->vals[w->n], v, vi); w->vals[w->n][vi] = '\0';
    w->n++;
}

/* Executes SELECT <field> FROM <table> WHERE k=v [AND …] LIMIT 1.
 * Returns 0=found (out_data heap-alloc'd; caller frees), -1=not found.
 * out_type is filled with SQLITE_TEXT(3) / SQLITE_BLOB(4) / SQLITE_INTEGER(1). */
static int run_query(const char *table, const char *field, const char *query_str,
                     void **out_data, size_t *out_len, int *out_type) {
    where_t w = {0};
    gly_uri_query_each(query_str, collect_kv, &w);

    char sql[512];
    int  pos = snprintf(sql, sizeof(sql), "SELECT %s FROM %s", field, table);
    for (int i = 0; i < w.n && pos < (int)sizeof(sql) - 1; i++)
        pos += snprintf(sql + pos, sizeof(sql) - (size_t)pos,
                        i ? " AND %s=?" : " WHERE %s=?", w.keys[i]);
    snprintf(sql + pos, sizeof(sql) - (size_t)pos, " LIMIT 1;");

    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    for (int i = 0; i < w.n; i++)
        sqlite3_bind_text(st, i + 1, w.vals[i], -1, SQLITE_TRANSIENT);

    int rc = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        int type = sqlite3_column_type(st, 0);
        if (out_type) *out_type = type;
        switch (type) {
        case SQLITE_TEXT: {
            const char *v    = (const char *)sqlite3_column_text(st, 0);
            size_t      l    = v ? strlen(v) : 0;
            char       *copy = malloc(l + 1);
            if (copy) {
                memcpy(copy, v ? v : "", l + 1);
                *out_data = copy;
                if (out_len) *out_len = l;
                rc = 0;
            }
            break;
        }
        case SQLITE_BLOB: {
            int   bytes = sqlite3_column_bytes(st, 0);
            void *copy  = malloc((size_t)bytes);
            if (copy) {
                memcpy(copy, sqlite3_column_blob(st, 0), (size_t)bytes);
                *out_data = copy;
                if (out_len) *out_len = (size_t)bytes;
                rc = 0;
            }
            break;
        }
        case SQLITE_INTEGER: {
            int64_t *v = malloc(sizeof(int64_t));
            if (v) {
                *v        = sqlite3_column_int64(st, 0);
                *out_data = v;
                if (out_len) *out_len = sizeof(int64_t);
                rc = 0;
            }
            break;
        }
        default:
            break;
        }
    }
    sqlite3_finalize(st);
    return rc;
}

/* ── db:// schema handler (registered with Daemon_Img on start) ───── */

static void db_schema_cb(const char *url, void *schema_usr,
                          gamely_img_on_fetch_cb on_done, void *on_done_usr) {
    (void)schema_usr;
    if (!on_done) return;

    char table[64], path_buf[128], field[64];
    gly_uri_host(url, table, sizeof(table));
    gly_uri_path(url, path_buf, sizeof(path_buf));
    const char *fp = path_buf[0] == '/' ? path_buf + 1 : path_buf;
    strncpy(field, fp, sizeof(field) - 1);
    field[sizeof(field) - 1] = '\0';

    const char *qs = gly_uri_query(url);

    void  *data = NULL;
    size_t len  = 0;
    int    type = 0;
    if (run_query(table, field, qs ? qs : "", &data, &len, &type) != 0) {
        on_done(NULL, 0, NULL, on_done_usr);
        return;
    }

    if (type == SQLITE_BLOB) {
        /* also fetch hint for this row */
        void *hint_v = NULL;
        run_query(table, "hint", qs ? qs : "", &hint_v, NULL, NULL);
        on_done((uint8_t *)data, len, (char *)hint_v, on_done_usr);
        free(hint_v);
        free(data);
    } else {
        /* TEXT result → redirect URL */
        on_done(NULL, 0, (char *)data, on_done_usr);
        free(data);
    }
}

/* ── public API ───────────────────────────────────────────────────── */

void gamely_daemon_db_start(void) {
    if (sqlite3_open("app.db", &s_db) != SQLITE_OK) {
        sqlite3_close(s_db);
        s_db = NULL;
        return;
    }
    exec_sql("CREATE TABLE IF NOT EXISTS persistent ("
             "  id    INTEGER PRIMARY KEY AUTOINCREMENT,"
             "  key   TEXT UNIQUE NOT NULL,"
             "  value TEXT);");
    exec_sql("CREATE TABLE IF NOT EXISTS media ("
             "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
             "  name      TEXT,"
             "  short     TEXT UNIQUE,"
             "  url       TEXT,"
             "  type      TEXT,"
             "  url_image TEXT);");
    exec_sql("CREATE TABLE IF NOT EXISTS blob ("
             "  id   INTEGER PRIMARY KEY AUTOINCREMENT,"
             "  data BLOB NOT NULL,"
             "  hint TEXT);");
    gecnd_registry("set", "image_resolver:db$0", db_schema_cb, NULL);
}

void gamely_daemon_db_stop(void) {
    if (!s_db) return;
    sqlite3_close(s_db);
    s_db = NULL;
}

int32_t gamely_daemon_db_insert_media(const char *name, const char *short_id,
                                       const char *url,  const char *type,
                                       const char *url_image) {
    if (!s_db) return -1;
    static const char sql[] =
        "INSERT INTO media (name, short, url, type, url_image) VALUES (?,?,?,?,?)"
        " ON CONFLICT(short) DO UPDATE SET"
        "  name=excluded.name, url=excluded.url,"
        "  type=excluded.type, url_image=excluded.url_image;";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, name,      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, short_id,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, url,       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, type,      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, url_image, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return (int32_t)sqlite3_last_insert_rowid(s_db);
}

void gamely_daemon_db_delete_media(const char *short_id) {
    if (!s_db) return;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "DELETE FROM media WHERE short=?;",
                            -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, short_id, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void gamely_daemon_db_delete_media_by_type(const char *type) {
    if (!s_db) return;
    sqlite3_stmt *st;
    const char *sql = (type && type[0])
        ? "DELETE FROM media WHERE type=?;"
        : "DELETE FROM media;";
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return;
    if (type && type[0]) sqlite3_bind_text(st, 1, type, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

int32_t gamely_daemon_db_insert_blob(const uint8_t *data, size_t len,
                                      const char *hint) {
    if (!s_db) return -1;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "INSERT INTO blob (data, hint) VALUES (?,?);",
                            -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_blob(st, 1, data, (int)len, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hint, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return (int32_t)sqlite3_last_insert_rowid(s_db);
}

void gamely_daemon_db_delete_blob(int32_t id) {
    if (!s_db) return;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "DELETE FROM blob WHERE id=?;",
                            -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void gamely_daemon_db_kv_set(const char *key, const char *value) {
    if (!s_db) return;
    static const char sql[] =
        "INSERT INTO persistent (key, value) VALUES (?,?)"
        " ON CONFLICT(key) DO UPDATE SET value=excluded.value;";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, key,   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

const char *gamely_daemon_db_kv_get(const char *key) {
    if (!s_db) return NULL;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db,
            "SELECT value FROM persistent WHERE key=? LIMIT 1;",
            -1, &st, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    const char *rc = NULL;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(st, 0);
        if (v) {
            strncpy(s_kv_buf, v, KV_BUF_CAP - 1);
            s_kv_buf[KV_BUF_CAP - 1] = '\0';
            rc = s_kv_buf;
        }
    }
    sqlite3_finalize(st);
    return rc;
}

char *gamely_daemon_db_media_json(const char *type) {
    if (!s_db) return NULL;
    static const char sel[] =
        "SELECT json_group_array(json_object("
        "'name',name,'short',short,'url',url,"
        "'type',type,'url_image',url_image)) FROM media";
    char sql[256];
    snprintf(sql, sizeof(sql), "%s%s;", sel, (type && type[0]) ? " WHERE type=?" : "");

    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return NULL;
    if (type && type[0]) sqlite3_bind_text(st, 1, type, -1, SQLITE_TRANSIENT);

    char *out = NULL;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(st, 0);
        size_t l = v ? strlen(v) : 0;
        if ((out = malloc(l + 1))) memcpy(out, v ? v : "", l + 1);
    }
    sqlite3_finalize(st);
    return out;
}

void *gamely_daemon_db_query_uri(const char *uri, size_t *out_len) {
    if (!s_db || !uri) return NULL;

    char table[64], path_buf[128], field[64];
    gly_uri_host(uri, table, sizeof(table));
    gly_uri_path(uri, path_buf, sizeof(path_buf));
    const char *fp = path_buf[0] == '/' ? path_buf + 1 : path_buf;
    strncpy(field, fp, sizeof(field) - 1);
    field[sizeof(field) - 1] = '\0';

    const char *qs = gly_uri_query(uri);

    void *out = NULL;
    if (run_query(table, field, qs ? qs : "", &out, out_len, NULL) == 0)
        return out;
    return NULL;
}
