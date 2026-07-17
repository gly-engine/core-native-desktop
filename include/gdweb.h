#ifndef GDWEB_H
#define GDWEB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint32_t gdweb_id_t;

typedef enum { GDWEB_WS_OPEN, GDWEB_WS_CLOSE, GDWEB_WS_MESSAGE } gdweb_ws_event_t;

typedef struct {
    gdweb_id_t   id;
    const char  *method;
    const char  *path;
    const char  *body;
    size_t       body_len;
} gdweb_http_req_t;

typedef struct {
    gdweb_id_t       id;
    gdweb_ws_event_t event;
    const char      *data;
    size_t           len;
    void           **usr;
} gdweb_ws_req_t;

typedef void (*gdweb_http_cb_t)  (const gdweb_http_req_t *req);
typedef void (*gdweb_ws_cb_t)    (const gdweb_ws_req_t   *req);
typedef void (*gdweb_stream_cb_t)(gdweb_id_t conn_id, bool connected);

typedef void (*gdweb_status_cb_t)  (gdweb_id_t id, int status,                   void *user);
typedef void (*gdweb_data_cb_t)    (gdweb_id_t id, const char *data, size_t len, void *user);
typedef void (*gdweb_done_cb_t)    (gdweb_id_t id,                               void *user);
typedef void (*gdweb_error_cb_t)   (gdweb_id_t id, const char *msg,              void *user);
typedef void (*gdweb_ws_open_cb_t) (gdweb_id_t id,                               void *user);
typedef void (*gdweb_ws_msg_cb_t)  (gdweb_id_t id, const char *data, size_t len, void *user);
typedef void (*gdweb_ws_close_cb_t)(gdweb_id_t id,                               void *user);

typedef union {
    int64_t     i64;
    const char *str;
} gdweb_value_t;

typedef enum {
    GDWEB_HTTP_NONE = 0,
    GDWEB_HTTP_STATUS,
    GDWEB_HTTP_CONTENT_TYPE,
    GDWEB_HTTP_STREAM,
} gdweb_http_cmd_t;

typedef struct {
    void (*http)    (gdweb_id_t id, gdweb_http_cmd_t cmd, const gdweb_value_t *value);
    void (*send)    (gdweb_id_t id, const char *data, size_t len);
    void (*send_all)(const char *path, const char *data, size_t len,
                     gdweb_id_t exclude_id);
    void (*start)   (void *loop, int port);
    void (*stop)    (void);
    /* encerra a conexao pelo lado servidor (close frame no ws);
     * close_all derruba todos os assinantes do path (NULL = todos) */
    void (*close)    (gdweb_id_t id);
    void (*close_all)(const char *path, gdweb_id_t exclude_id);
} gdweb_server_t;

typedef struct {
    gdweb_id_t (*http)      (const char *url, gdweb_http_req_t *req,
                             gdweb_status_cb_t on_status, gdweb_data_cb_t on_data,
                             gdweb_done_cb_t on_done, gdweb_error_cb_t on_error,
                             void *user);
    gdweb_id_t (*ws_connect)(const char *url, const char *protocol,
                             gdweb_ws_open_cb_t on_open, gdweb_ws_msg_cb_t on_msg,
                             gdweb_ws_close_cb_t on_close, gdweb_error_cb_t on_error,
                             void *user);
    void       (*send)      (gdweb_id_t id, const char *data, size_t len);
    void       (*close)     (gdweb_id_t id);
    void       (*start)     (void *loop);
    void       (*stop)      (void);
} gdweb_client_t;

const gdweb_server_t *gdweb_control_server(void);
const gdweb_client_t *gdweb_control_client(void);

void gdweb_loop_start(void *loop);
void gdweb_loop_stop (void);

void gdweb_lua_tick(void);

#endif
