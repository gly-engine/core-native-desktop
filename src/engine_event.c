#include "engine/bytecode.h"
#include "game/bytecode.h"
#include "zeebo.h"

static int native_callback_loop;
static int native_callback_draw;
static int native_callback_keyboard;

/**
 * @param [in] file_name
 * @param [out] buf
 * @param [out] len
 */
static bool load_script(char *file_name, const char **buf, unsigned int *len) {
    bool success = true;
    char *file_buf = NULL;
    FILE *file_ptr = NULL;
    unsigned int file_len = 0;

    do {
        if (!file_name) {
            break;
        }

        success = false;
        file_ptr = fopen(file_name, "r");

        if (!file_ptr) {
            break;
        }

        do {
            file_buf = realloc(file_buf, file_len + 4096);
            file_len += fread(file_buf + file_len, 1, 4096, file_ptr);
        } while (!feof(file_ptr));

        file_buf[file_len] = '\0';
        *buf = file_buf;
        *len = file_len;

        success = true;
    } while (0);

    return success;
}

static void engine_init() {
    lua_State *L = lua();

    const char *game = game_bytecode_lua;
    unsigned int game_len = game_bytecode_lua_len;

    const char *engine = engine_bytecode_lua;
    unsigned int engine_len = engine_bytecode_lua_len;

    do {
        if (!load_script(kernel_option.game, &game, &game_len)) {
            kernel_add_error("Cannot open file:");
            kernel_add_error(kernel_option.game);
            break;
        }

        if (!load_script(kernel_option.engine, &engine, &engine_len)) {
            kernel_add_error("Cannot open file:");
            kernel_add_error(kernel_option.engine);
            break;
        }

        if (game_len == 0 && !load_script("game.lua", &game, &game_len)) {
            kernel_add_error("copy your game.lua near the executable folder!");
            break;
        }

        luaL_loadbuffer(L, engine, engine_len, "engine");
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            kernel_add_error(lua_tostring(L, -1));
            break;
        }

        lua_getglobal(L, "native_callback_init");
        lua_pushnumber(L, kernel_option.width);
        lua_pushnumber(L, kernel_option.height);

        luaL_loadbuffer(L, game, game_len, "game");
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            kernel_add_error(lua_tostring(L, -1));
            break;
        }
        if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
            kernel_add_error(lua_tostring(L, -1));
            break;
        }
    } while (0);

    if (game && game != game_bytecode_lua) {
        free(game);
    }

    if (engine && engine != engine_bytecode_lua) {
        free(engine);
    }

    lua_getglobal(L, "native_callback_loop");
    native_callback_loop = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_getglobal(L, "native_callback_draw");
    native_callback_draw = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_getglobal(L, "native_callback_keyboard");
    native_callback_keyboard = luaL_ref(L, LUA_REGISTRYINDEX);
}

static void engine_exit() {
    lua_State *L = lua();
    if (native_callback_loop) {
        luaL_unref(L, LUA_REGISTRYINDEX, native_callback_loop);
    }
    if (native_callback_draw) {
        luaL_unref(L, LUA_REGISTRYINDEX, native_callback_draw);
    }
    if (native_callback_keyboard) {
        luaL_unref(L, LUA_REGISTRYINDEX, native_callback_keyboard);
    }
}

static void engine_loop() {
    lua_State *L = lua();
    lua_rawgeti(L, LUA_REGISTRYINDEX, native_callback_loop);
    lua_pushnumber(L, kernel_time.dt);
    lua_pcall(L, 1, 0, 0);
}

static void engine_draw() {
    lua_State *L = lua();
    lua_rawgeti(L, LUA_REGISTRYINDEX, native_callback_draw);
    lua_pcall(L, 0, 0, 0);
}

void engine_keypress(const char *const key, uint8_t value) {
    lua_State *L = lua();
    lua_rawgeti(L, LUA_REGISTRYINDEX, native_callback_keyboard);
    lua_pushstring(L, key);
    lua_pushinteger(L, value);
    lua_pcall(L, 2, 0, 0);
}

void engine_install() {
    kernel_event_install(KERNEL_EVENT_INIT, engine_init);
    kernel_event_install(KERNEL_EVENT_UPDATE, engine_loop);
    kernel_event_install(KERNEL_EVENT_DRAW, engine_draw);
    kernel_event_install(KERNEL_EVENT_EXIT, engine_exit);
}
