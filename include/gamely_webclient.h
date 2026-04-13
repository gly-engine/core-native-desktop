#ifndef GAMELY_WEBCLIENT_H
#define GAMELY_WEBCLIENT_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t gly_wc_id_t;

typedef void (*gly_wc_status_cb)(gly_wc_id_t id, int status,                           void *user);
typedef void (*gly_wc_data_cb)  (gly_wc_id_t id, const char *data,  size_t len,         void *user);
typedef void (*gly_wc_done_cb)  (gly_wc_id_t id,                                        void *user);
typedef void (*gly_wc_error_cb) (gly_wc_id_t id, const char *msg,                       void *user);

typedef void (*gly_wc_ws_open_cb) (gly_wc_id_t id,                                      void *user);
typedef void (*gly_wc_ws_msg_cb)  (gly_wc_id_t id, const char *data, size_t len,        void *user);
typedef void (*gly_wc_ws_close_cb)(gly_wc_id_t id,                                      void *user);

void gamely_daemon_webclient_start(void *loop);
void gamely_daemon_webclient_stop(void);

gly_wc_id_t gamely_daemon_webclient_http(
    const char      *method,
    const char      *url,
    const char      *body,
    gly_wc_status_cb on_status,
    gly_wc_data_cb   on_data,
    gly_wc_done_cb   on_done,
    gly_wc_error_cb  on_error,
    void            *user
);

gly_wc_id_t gamely_daemon_webclient_ws_connect(
    const char        *url,
    const char        *protocol,
    gly_wc_ws_open_cb  on_open,
    gly_wc_ws_msg_cb   on_msg,
    gly_wc_ws_close_cb on_close,
    gly_wc_error_cb    on_error,
    void              *user
);

void gamely_daemon_webclient_ws_send (gly_wc_id_t id, const char *data, size_t len);
void gamely_daemon_webclient_ws_close(gly_wc_id_t id);

#endif
