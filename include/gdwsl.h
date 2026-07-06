#ifndef GDWSL_H
#define GDWSL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint32_t gly_req_id_t;

typedef enum { GLY_WS_OPEN, GLY_WS_CLOSE, GLY_WS_MESSAGE } gly_ws_event_t;

typedef struct {
    gly_req_id_t  id;
    const char   *method;
    const char   *path;
    const char   *body;
    size_t        body_len;
} gly_http_req_t;

typedef struct {
    gly_req_id_t   id;
    gly_ws_event_t event;
    const char    *data;
    size_t         len;
    void         **usr;
} gly_ws_req_t;

typedef void (*gly_http_cb_t)   (const gly_http_req_t *req);
typedef void (*gly_ws_cb_t)     (const gly_ws_req_t   *req);
typedef void (*gly_stream_cb_t) (gly_req_id_t conn_id, bool connected);

typedef void (*gly_wc_status_cb)  (gly_req_id_t id, int status,                   void *user);
typedef void (*gly_wc_data_cb)    (gly_req_id_t id, const char *data, size_t len, void *user);
typedef void (*gly_wc_done_cb)    (gly_req_id_t id,                               void *user);
typedef void (*gly_wc_error_cb)   (gly_req_id_t id, const char *msg,              void *user);
typedef void (*gly_wc_ws_open_cb) (gly_req_id_t id,                               void *user);
typedef void (*gly_wc_ws_msg_cb)  (gly_req_id_t id, const char *data, size_t len, void *user);
typedef void (*gly_wc_ws_close_cb)(gly_req_id_t id,                               void *user);

typedef union {
    int64_t     i64;
    const char *str;
} gdwsl_value_t;

typedef enum {
    GDWSL_HTTP_NONE = 0,
    GDWSL_HTTP_STATUS,        /* value->i64 */
    GDWSL_HTTP_CONTENT_TYPE,  /* value->str */
    GDWSL_HTTP_STREAM,        /* value NULL — upgrade p/ chunked stream */
} gdwsl_http_cmd_t;

typedef struct {
    void (*http)    (gly_req_id_t id, gdwsl_http_cmd_t cmd, const gdwsl_value_t *value);
    void (*send)    (gly_req_id_t id, const char *data, size_t len);
    void (*send_all)(const char *path, const char *data, size_t len,
                     gly_req_id_t exclude_id);
    void (*start)   (void *loop, int port);
    void (*stop)    (void);
} gdwsl_server_t;

typedef struct {
    gly_req_id_t (*http)      (const char *url, gly_http_req_t *req,
                               gly_wc_status_cb on_status, gly_wc_data_cb on_data,
                               gly_wc_done_cb on_done, gly_wc_error_cb on_error,
                               void *user);
    gly_req_id_t (*ws_connect)(const char *url, const char *protocol,
                               gly_wc_ws_open_cb on_open, gly_wc_ws_msg_cb on_msg,
                               gly_wc_ws_close_cb on_close, gly_wc_error_cb on_error,
                               void *user);
    void         (*send)      (gly_req_id_t id, const char *data, size_t len);
    void         (*close)     (gly_req_id_t id);
    void         (*start)     (void *loop);
    void         (*stop)      (void);
} gdwsl_client_t;

const gdwsl_server_t *gdwsl_control_server(void);
const gdwsl_client_t *gdwsl_control_client(void);

void gdwsl_loop_start(void *loop);
void gdwsl_loop_stop (void);

void gamely_daemon_webserver_lua_tick(void);
void gamely_daemon_webclient_img_register(void);

#endif
