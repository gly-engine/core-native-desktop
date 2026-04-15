#ifndef GAMELY_WEBSERVER_H
#define GAMELY_WEBSERVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint32_t gly_conn_id_t;

typedef enum { GLY_WS_OPEN, GLY_WS_CLOSE, GLY_WS_MESSAGE } gly_ws_event_t;

typedef struct {
    gly_conn_id_t  conn_id;
    const char    *path;
} gly_http_req_t;

typedef struct {
    gly_conn_id_t  conn_id;
    gly_ws_event_t event;
    const char    *data;
    size_t         len;
} gly_ws_req_t;

typedef void (*gly_http_cb_t)(const gly_http_req_t *req);
typedef void (*gly_ws_cb_t)(const gly_ws_req_t *req);

/* callback(conn_id, true) = novo cliente; callback(conn_id, false) = desconectado */
typedef void (*gly_stream_cb_t)(gly_conn_id_t conn_id, bool connected);

void gamely_daemon_webserver_route_http  (const char *path, gly_http_cb_t cb);
void gamely_daemon_webserver_route_ws   (const char *path, gly_ws_cb_t cb);
void gamaly_daemon_webserver_route_stream(const char *path, gly_stream_cb_t cb);
void gamely_daemon_webserver_proxy_http  (const char *from, const char *to);
void gamely_daemon_webserver_proxy_ws   (const char *from, const char *to);
void gamely_daemon_webserver_start       (void *loop, int port);
void gamely_daemon_webserver_stop        (void);

void gamely_ws_send(const char    *path,
                    gly_conn_id_t  conn_id,
                    const char    *text,
                    size_t         len,
                    gly_conn_id_t  exclude_id);

void gamely_http_respond(gly_conn_id_t  conn_id,
                         int            status,
                         const char    *content_type,
                         const char    *body,
                         size_t         body_len);

#endif /* GAMELY_WEBSERVER_H */
