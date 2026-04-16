#ifndef GECND_H
#define GECND_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define GLY_REGISTRYINDEX ((uint32_t)(uintptr_t)(gecnd_new))
#define GECND_FLAG_NONE   (0u)
#define GECND_FLAG_TIMER_FIXED          (0u)
#define GECND_FLAG_TIMER_INTERNAL       (1u)
#define GECND_FLAG_TIMER_BACKEND        (2u)
#define GECND_FLAG_TIMER_PREFER_BACKEND (3u)

#ifndef DOXYGEN
#define GECND_INTERNAL_MALLOC           (1u)
#define GECND_INTERNAL_RUNNING          (2u)
#define GECND_INTERNAL_WANT_EXIT        (4u)
#define GECND_INTERNAL_BROWSER          (8u)
#endif

// alias:
#define gecnd_add_flags(gly)  gecnd_set_flags(gly, gecnd_get_flags(gly) | FLAG_A)
#define gecnd_del_flags(gly)  gecnd_set_flags(gly, gecnd_get_flags(gly) & ~FLAG_A);

typedef struct lua_State lua_State;

typedef struct {
    lua_State *L;
    void* loop;
    uint8_t target_fps;
    uint8_t frameskip;
    uint8_t frameskip_count;
    uint8_t flags;
    uint8_t internal;
    int16_t width;
    int16_t height;
    int16_t window_width;
    int16_t window_height;
    int16_t delta_time;
    uint16_t port;
    float scale_factor;
    int ref_native_callback_init;
    int ref_native_callback_loop;
    int ref_native_callback_draw;
    int ref_native_callback_keyboard;
    bool want_blit;
    bool disable_radius;
    char *lua_game_code;
    char *lua_engine_code;
    char *browser_bin;
    const char *input;       // URI do input ativo; default "void://0"
    const char* error_string;
} gecnd_t;

// instance
gecnd_t *gecnd_new(lua_State* L);
gecnd_t *gecnd_get_root();
bool gecnd_is_root(gecnd_t *gly);
void gecnd_destroy(gecnd_t *gly);

// configure
void gecnd_set_loop(gecnd_t *gly, void* loop);
void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]);
void gecnd_set_delta(gecnd_t *gly, int16_t ms);
void gecnd_set_flags(gecnd_t *gly, int32_t flags);
void gecnd_set_screensize(gecnd_t *gly, int16_t width, int16_t height);
// status
uint32_t gecnd_get_flags(gecnd_t *gly);
uint32_t gecnd_get_sleep(gecnd_t *gly);
// error
bool gecnd_has_errors(gecnd_t *gly);
const char* gecnd_get_errors(gecnd_t *gly);
// tick
bool gecnd_update(gecnd_t *gly);
void gecnd_dispatch_key_event(gecnd_t *gly, const char *name, bool pressed, int port);
// utils
uint32_t gecnd_get_delta_ms(void);
uint64_t gecnd_get_cur_time(void);
size_t gecnd_utils_get_exe_cwd(char *buffer, size_t max_size);
size_t gecnd_utils_get_cwd(char *buffer, size_t max_size);
// filters
void gecnd_filter_set_brightness(float v);
void gecnd_filter_set_contrast(float v);
void gecnd_filter_set_saturation(float v);
void gecnd_filter_set_film_grain(float v);
void gecnd_filter_set_crt(float v);
void gecnd_filter_set_scratch(float v);
void gecnd_filter_set_jitter(float v);
void gecnd_filter_set_video_pos(float x, float y, float w, float h);
void gecnd_filter_set_rotation(float angle);
void gecnd_filter_set_aa(float blur, float wC, float wN);
void gecnd_filter_set_corners(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4);
void gecnd_filter_reset_effects();
void gecnd_filter_reset_corners();
void gecnd_filter_reset_video_pos();
bool gencd_filter_is_zero_corners();
bool gencd_filter_is_zero_video_pos();

/* ---- Web Daemons ---- */

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

void gamely_daemon_webserver_start(void *loop, int port);
void gamely_daemon_webserver_stop(void);
void gamely_daemon_webserver_http_send(gly_req_id_t id, int status,
        const char *content_type, const char *body, size_t body_len);
void gamely_daemon_webserver_ws_send    (gly_req_id_t id, const char *data, size_t len);
void gamely_daemon_webserver_ws_send_all(const char *path, const char *data, size_t len,
        gly_req_id_t exclude_id);
void gamely_daemon_webserver_stream_write(gly_req_id_t id, const uint8_t *buf, int size);

void gamely_daemon_webloop_start(void *loop);
void gamely_daemon_webloop_stop(void);
void gamely_daemon_webloop_route_http  (const char *path, gly_http_cb_t cb);
void gamely_daemon_webloop_route_ws    (const char *path, gly_ws_cb_t cb);
void gamely_daemon_webloop_route_stream(const char *path, const char *content_type,
                                        gly_stream_cb_t cb);
void gamely_daemon_webloop_route_proxy (const char *from, const char *to);

void         gamely_daemon_webclient_start(void *loop);
void         gamely_daemon_webclient_stop(void);
gly_req_id_t gamely_daemon_webclient_http(const char *url, gly_http_req_t *req,
    gly_wc_status_cb on_status, gly_wc_data_cb on_data,
    gly_wc_done_cb on_done, gly_wc_error_cb on_error, void *user);
gly_req_id_t gamely_daemon_webclient_ws_connect(const char *url, const char *protocol,
    gly_wc_ws_open_cb on_open, gly_wc_ws_msg_cb on_msg,
    gly_wc_ws_close_cb on_close, gly_wc_error_cb on_error, void *user);
void gamely_daemon_webclient_ws_send (gly_req_id_t id, const char *data, size_t len);
void gamely_daemon_webclient_ws_close(gly_req_id_t id);

#endif
