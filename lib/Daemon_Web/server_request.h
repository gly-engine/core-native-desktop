#ifndef GDWEB_SERVER_REQUEST_H
#define GDWEB_SERVER_REQUEST_H

#include <stdlib.h>
#include <string.h>

#include "gdweb.h"

#define SERVER_REQUEST_LIMIT (64u * 1024u)

typedef struct {
    gdweb_http_cb_t callback;
    const char *method;
    char path[512];
    char *body;
    size_t length;
    size_t capacity;
} server_request_t;

static void server_request_clear(server_request_t *request) {
    free(request->body);
    request->body = NULL;
    request->length = 0;
    request->capacity = 0;
    request->callback = NULL;
}

static int server_request_begin(server_request_t *request, gdweb_http_cb_t callback,
                                const char *method, const char *path,
                                unsigned long long length) {
    if (length > SERVER_REQUEST_LIMIT) return 413;
    request->callback = callback;
    request->method = method;
    memcpy(request->path, path, strlen(path) + 1);
    request->body = malloc((size_t)length + 1);
    if (!request->body) return 500;
    request->capacity = (size_t)length;
    return 0;
}

static int server_request_append(server_request_t *request, const void *data, size_t length) {
    if (length > request->capacity - request->length) return 413;
    size_t needed = request->length + length;
    if (length) {
        memcpy(request->body + request->length, data, length);
        request->length = needed;
        request->body[needed] = '\0';
    }
    return 0;
}

static void server_request_dispatch(server_request_t *request, gdweb_id_t id) {
    gdweb_http_req_t req = {
        .id = id,
        .path = request->path,
        .method = request->method,
        .body = request->body,
        .body_len = request->length,
    };
    request->callback(&req);
    server_request_clear(request);
}

#endif
