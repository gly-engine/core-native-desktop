#include <lua.h>
#include <lauxlib.h>
#include <sqlite3.h>

static sqlite3 *db = NULL;

static int lua_native_storage_set(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    const char *value = luaL_checkstring(L, 2);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO persistent (key,value) VALUES (?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value;", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    (void) sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    lua_settop(L, 0);
    return 0;
}

static int lua_native_storage_get(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT value FROM persistent WHERE key=?;", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);

    const char *value = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = (const char*)sqlite3_column_text(stmt, 0);

    sqlite3_finalize(stmt);

    lua_pushvalue(L, 2);
    if (value) {
        lua_pushstring(L, value);
    }
    else {
        lua_pushnil(L);
    };
    lua_call(L, 1, 0);

    lua_settop(L, 0);
    return 0;
}


void gly_hook_luaopen_storage(lua_State *L) {
    if (sqlite3_open("app.db", &db) != SQLITE_OK) {
        db = NULL;
        return;
    }

    static const char query[] = "CREATE TABLE IF NOT EXISTS persistent (id INTEGER PRIMARY KEY AUTOINCREMENT, key TEXT UNIQUE, value TEXT);";
    char *errmsg = NULL;
    int rc;

    rc = sqlite3_exec(db, query, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        if (errmsg) sqlite3_free(errmsg);
        sqlite3_close(db);
        db = NULL;
        return;
    }

    const luaL_Reg funcs[] = {
        {"native_storage_set", lua_native_storage_set},
        {"native_storage_get", lua_native_storage_get},
        {NULL, NULL}
    };
    
    for (int i = 0; funcs[i].name != NULL; i++) {
        lua_register(L, funcs[i].name, funcs[i].func);
    }
}

void gly_hook_luaclose_storage(lua_State *L) {
    (void) L;

    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}
