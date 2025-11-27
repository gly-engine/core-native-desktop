#include <math.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <khash.h>

#include "gecnd.h"
#include "gehook.h"

KHASH_MAP_INIT_STR(str2int, int32_t)
static khash_t(str2int) *cache = NULL;
static int32_t next_id = 1;

static int32_t image_load(const char *src) {
    bool success = false;

    if (!cache)
        cache = kh_init(str2int);

    khiter_t k = kh_get(str2int, cache, src);
    if (k != kh_end(cache)) {
        return kh_value(cache, k);
    }

    int32_t new_id = next_id;

    do {
        native_image_load(src, new_id, &success);
        if (success)
            break;

        const char *cwd = gecnd_utils_get_exe_cwd();
        if (cwd == NULL)
            break;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", cwd, src);

        native_image_load(full_path, new_id, &success);
    } while (0);

    if (!success) {
        return -1;
    }

    int ret;
    char *key = strdup(src);
    k = kh_put(str2int, cache, key, &ret);
    kh_value(cache, k) = new_id;
    next_id++;

    return new_id;
}

static int lua_native_image_unload(lua_State *L) {
    int32_t img_id = -1;
    bool success = false;
    khiter_t k = kh_end(cache);

    if (!cache) {
        return 0;
    }

    if (lua_type(L, 1) == LUA_TSTRING) {
        const char *img_src = lua_tostring(L, 1);
        k = kh_get(str2int, cache, img_src);
        if (k != kh_end(cache)) {
            img_id = kh_value(cache, k);
        }
    } 
    else if (lua_type(L, 1) == LUA_TNUMBER) {
        img_id = (int32_t) lua_tointeger(L, 1);
        khiter_t iter;
        for (iter = kh_begin(cache); iter != kh_end(cache); ++iter) {
            if (kh_exist(cache, iter) && kh_value(cache, iter) == img_id) {
                k = iter;
                break;
            }
        }
    } 
    else {
        return luaL_error(L, "expected string or integer for image unload");
    }

    if (img_id != -1 && k != kh_end(cache)) {
        native_image_unload(img_id, &success);
        if (success) {
            free((char *)kh_key(cache, k));
            kh_del(str2int, cache, k);
        }
    }

    lua_pushboolean(L, success);
    return 1;
}

static int lua_native_image_load(lua_State *L) {
    const char *img_src = luaL_checkstring(L, 1);
    int32_t img_id = image_load(img_src);
    lua_settop(L, 0);
    lua_pushnumber(L, img_id);
    return 1;
}

static int lua_native_image_exists(lua_State *L) {
    const char *img_src = luaL_checkstring(L, 1);
    int32_t img_id = image_load(img_src);
    lua_settop(L, 0);
    lua_pushboolean(L, img_id != -1);
    return 1;
}

static int lua_native_image_draw(lua_State *L) {
    int32_t img_id = -1;

    if (lua_type(L, 1) == LUA_TSTRING) {
        const char *img_src = lua_tostring(L, 1);
        img_id = image_load(img_src);
    } else if (lua_type(L, 1) == LUA_TNUMBER) {
        img_id = (int32_t)lua_tointeger(L, 1);
    } else {
        return luaL_error(L, "expected string or integer for image id");
    }

    if (img_id != -1) {
        int16_t x = (int16_t)luaL_checknumber(L, 2);
        int16_t y = (int16_t)luaL_checknumber(L, 3);
        native_image_draw(img_id, x, y);
    }

    lua_settop(L, 0);
    lua_pushinteger(L, img_id);
    return 1;
}

static int lua_native_image_mensure(lua_State *L) {
    int32_t img_id = -1;

    if (lua_type(L, 1) == LUA_TSTRING) {
        const char *img_src = lua_tostring(L, 1);
        img_id = image_load(img_src);
    } else if (lua_type(L, 1) == LUA_TNUMBER) {
        img_id = (int32_t)lua_tointeger(L, 1);
    } else {
        return luaL_error(L, "expected string or integer for image id");
    }

    int16_t w = 0, h = 0;

    if (img_id != -1) {
        native_image_mensure(img_id, &w, &h);
    }

    lua_settop(L, 0);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

const luaL_Reg frontend_api_image[] = {
               {"native_image_load", lua_native_image_load},
               {"native_image_draw", lua_native_image_draw},
               {"native_image_exists", lua_native_image_exists},
               {"native_image_mensure", lua_native_image_mensure},
               {"native_image_unload", lua_native_image_unload},
               {NULL, NULL}};
