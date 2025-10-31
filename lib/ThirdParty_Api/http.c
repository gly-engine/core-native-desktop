#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <curl/curl.h>
#include <lauxlib.h>
#include <lua.h>

typedef struct {
    lua_State *L;
    int64_t id;
} req_t;

static void native_callback_http(lua_State* L, int64_t req_id, const char *const evt_key)
{
    static int native_http_callback;
    if (!native_http_callback) {
        lua_getglobal(L, "native_callback_http");
        native_http_callback = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, native_http_callback);
    lua_pushinteger(L, req_id);
    lua_pushstring(L, evt_key);
}

static const char* native_http_get_str(lua_State* L, int64_t req_id, const char *const evt_key)
{
    native_callback_http(L, req_id, evt_key);
    lua_pcall(L, 2, 1, 0);
    return luaL_checkstring(L, -1);
}

static size_t curl_callback_http(void *contents, size_t size, size_t nmemb, void *userp)
{
    req_t *req = (req_t *)userp;
    size_t total_size = size * nmemb;
    native_callback_http(req->L, req->id, "add-body-data");
    lua_pushlstring(req->L, (char*) contents, total_size);
    lua_pcall(req->L, 3, 0, 0);
    return total_size;
}

static size_t curl_callback_header(void *contents, size_t size, size_t nmemb, void *userp) {
    char http_status[] = "000";
    req_t *req = (req_t *)userp;
    size_t total_size = size * nmemb;

    do {
        if (strncmp("HTTP/", (char *)contents, 5) == 0) {
            memcpy(http_status, contents + 9, sizeof(http_status) - 1);
            int status = atoi(http_status);
            native_callback_http(req->L, req->id, "set-status");
            lua_pushinteger(req->L, status);
            lua_pcall(req->L, 3, 0, 0);            
        }
    } while (0);

    return total_size;
}

static int lua_native_http_handler(lua_State* L)
{
    CURL *curl = NULL;
    CURLcode res;

    int64_t req_id = luaL_checkinteger(L, 2);
    req_t request = {L, req_id};

    const char *url = native_http_get_str(L, req_id, "get-fullurl");
    const char *method = native_http_get_str(L, req_id, "get-method");
    const char *body = native_http_get_str(L, req_id, "get-body-data");

    do {
        curl = curl_easy_init();

        if (!curl) {
            native_callback_http(L, req_id, "set-error");
            lua_pushstring(L, "failed to init curl\n");
            lua_pcall(L, 3, 0, 0);
            break;
        }

        do {
            if (strcmp(method, "GET") == 0) {
                curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
                break;
            }
            if (strcmp(method, "HEAD") == 0) {
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                break;
            }
            if (body != NULL && strlen(body) > 0) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            }
            if (strcmp(method, "POST") == 0) {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                break;
            } else if (strcmp(method, "PUT") == 0) {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
                break;
            } else if (strcmp(method, "DELETE") == 0) {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
                break;
            } else if (strcmp(method, "PATCH") == 0) {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
                break;
            }
        }
        while(0);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback_http);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &request);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_callback_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &request);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            native_callback_http(L, req_id, "set-error");
            lua_pushstring(L, curl_easy_strerror(res));
            lua_pcall(L, 3, 0, 0);
            break;
        }
    } while (0);

    return 0;
}

void gly_hook_luaopen_http(lua_State* L)
{
    lua_pushcfunction(L, lua_native_http_handler);
    lua_setglobal(L, "native_http_handler");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_ssl");

    lua_pushboolean(L, true);
    lua_setglobal(L, "native_http_has_callback");

    curl_global_init(CURL_GLOBAL_DEFAULT);
}
