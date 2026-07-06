#ifndef GECND_H
#define GECND_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

#define GLY_REGISTRYINDEX ((uint32_t)(uintptr_t)(gecnd_new))
#define GECND_FLAG_NONE   (0u)
#define GECND_FLAG_IMG_MOVE (1u << 0)
#define GECND_FLAG_TIMER_FIXED          (0u)
#define GECND_FLAG_TIMER_INTERNAL       (1u)
#define GECND_FLAG_TIMER_BACKEND        (2u)
#define GECND_FLAG_TIMER_PREFER_BACKEND (3u)

#define GECND_NATIVE_STUB(name, args);\
    static void stub_##name args {}   \
    void (*name) args = stub_##name;

#define GECND_NATIVE_DAEMON(name, args);\
    static void daemon_##name args;     \
    void (*name) args = daemon_##name;

#ifndef DOXYGEN
#define GECND_INTERNAL_HW_GL_READY      (16u)
#endif

typedef enum __attribute__((packed)) {
    GECND_TYPE_VOID,
    GECND_TYPE_STRING,
    GECND_TYPE_BOOLEAN,
    GECND_TYPE_U8,
    GECND_TYPE_U16,
    GECND_TYPE_U32,
    GECND_TYPE_U64,
    GECND_TYPE_I8,
    GECND_TYPE_I16,
    GECND_TYPE_I32,
    GECND_TYPE_I64,
    GECND_TYPE_F32,
    GECND_TYPE_F64,
} gecnd_type_t;

typedef enum __attribute__((packed)) {
    GECND_FSM_BOOT = 0,
    GECND_FSM_ARGS_PARSED,
    GECND_FSM_DAEMONS_UP,
    GECND_FSM_FETCHING,
    GECND_FSM_LUALIB_LOADED,
    GECND_FSM_ENGINE_LOADED,
    GECND_FSM_GAME_LOADED,
    GECND_FSM_RUNNING,
    GECND_FSM_RUNNING_PERFORMANCE,
    GECND_FSM_RUNNING_BACKGROUND,
    GECND_FSM_RUNNING_STANDBY,
    GECND_FSM_RUNNING_NOGAME,
    GECND_FSM_EXITING,
    GECND_FSM_EXITING_FORCE,
} gecnd_fsm_t;

typedef enum {
    GECND_LUA_SOURCE_NONE = 0,    /* nada definido — usa fallback FS */
    GECND_LUA_SOURCE_FILE,
    GECND_LUA_SOURCE_HTTP,
    GECND_LUA_SOURCE_VENDOR,
} gecnd_lua_source_kind_t;

typedef struct {
    gecnd_lua_source_kind_t kind;
    const char             *uri;
    union {
        struct {
            const uint8_t *buf;
            size_t         len;
        } embedded;                  /* kind == GECND_LUA_SOURCE_VENDOR */
        struct {
            uint8_t *buf;
            size_t   len;
            bool     done;
            bool     error;
        } fetch;                     /* kind == GECND_LUA_SOURCE_HTTP   */
    };
} gecnd_lua_source_t;

// alias:
#define gecnd_add_flags(gly)  gecnd_set_flags(gly, gecnd_get_flags(gly) | FLAG_A)
#define gecnd_del_flags(gly)  gecnd_set_flags(gly, gecnd_get_flags(gly) & ~FLAG_A);

typedef enum {
    GECND_PIX_FMT_RGBA8888 = 0,
    GECND_PIX_FMT_YUV420P  = 1,
    GECND_PIX_FMT_RGB565   = 2,
    GECND_PIX_FMT_RGBA5551 = 3,
    GECND_PIX_FMT_ETC1     = 4,
    GECND_PIX_FMT_ALPHA8   = 5,
    GECND_PIX_FMT_NONE     = -1
} GECNDColorFormat;

typedef struct {
    uint8_t    *data[4];
    int         linesize[4];
    int         width;
    int         height;
    int         format;
    double      pts;
    atomic_bool ready;
} MediaFrame;

typedef struct lua_State lua_State;

typedef struct {
    lua_State  *L;
    void       *loop;
    uint8_t     target_fps;
    uint8_t     frameskip;
    uint8_t     frameskip_count;
    uint8_t     flags;
    uint8_t     internal;       /* GECND_INTERNAL_HW_GL_READY */
    gecnd_fsm_t state;
    int16_t     width;
    int16_t     height;
    int16_t     delta_time;
    int         ref_native_callback_loop;
    int         ref_native_callback_draw;
    int         ref_native_callback_keyboard;
    bool        want_blit;
    gecnd_lua_source_t engine_source;
    gecnd_lua_source_t game_source;
    char       *error_buf;
    size_t      error_len;
    size_t      error_cap;
} gecnd_t;

typedef struct {
    int16_t  window_width;
    int16_t  window_height;
    uint16_t port;
    bool     disable_radius;
    bool     mojibake;
    float    font_factor;
    char     game_base_url[512];
} gecnd_display_t;

/**
 * @brief Tagged scalar/string value shared by the rdsl iterator and the ffi
 *        marshaller.
 *
 * The anonymous struct exposes `.ptr`/`.len` for string-like values; the scalar
 * members alias the same storage for numeric values.
 */
typedef union {
    uint8_t  u8;
    int8_t   i8;
    uint16_t u16;
    int16_t  i16;
    uint32_t u32;
    int32_t  i32;
    uint64_t u64;
    int64_t  i64;
    float    f32;
    double   f64;
    struct {
        void  *ptr;
        size_t len;
    };
} gly_any_t;

typedef enum {
    GECND_URL_KIND_NONE = 0,
    GECND_URL_KIND_SCHEME,
    GECND_URL_KIND_HOST,
    GECND_URL_KIND_PORT,
    GECND_URL_KIND_PATH,
    GECND_URL_KIND_PARAM,
    GECND_URL_KIND_FRAGMENT,
} gecnd_lang_url_kind_t;

/* Unified iterator state; see lib/Common_Language/{util,rdsl,url_iterator}.c.
 * Brace-init the header: gecnd_lang_t ctx = {{ "rdsl", pattern, text }};
 * then walk with gecnd_lang(&ctx). The small header survives across calls; the
 * per-engine states share storage through the union. */
typedef struct gecnd_lang {
    struct {
        union {
            const char *_lang;   /* selector before the first call */
            bool      (*_fn)(struct gecnd_lang *);   /* resolved engine after */
        };
        const char *pattern;
        const char *text;
        struct {
            uint8_t error    : 1;
            uint8_t started  : 1;
            uint8_t finished : 1;
            uint8_t reset    : 1;   /* keep lang/pattern/text, rewind the rest */
        };
    };
    union {
        struct {
            const char  *ptr;
            const char  *tptr;      /* text cursor (match mode) */
            gly_any_t    val;
            gecnd_type_t kind;
            int8_t       len;
            int8_t       keyidx;    /* ':' namespace depth */
            int8_t       plusidx;   /* current '+' group */
            int8_t       typeidx;   /* '$' type in group; -1 on keywords */
            uint8_t      score;
        } rdsl;
        struct {
            const char           *ptr;
            const char           *cur;    /* internal parse cursor */
            gly_any_t             val;
            size_t                len;
            gecnd_lang_url_kind_t kind;
            int8_t                idx;    /* ordinal within the current kind */
            uint8_t               phase;  /* internal parse phase */
        } url;
        gly_any_t result;   /* one-shot alias output (url:param, file:ext, …) */
    };
} gecnd_lang_t;

bool gecnd_lang(gecnd_lang_t *const ctx);

typedef void (*gecnd_registry_handler)(const char *key, void *value, void *usr);

int gecnd_registry(const char* cmd, const char *key, void *const value, void *const usr);

// plugins
bool gecnd_plugin_load(gecnd_t *gly, const char *path);
const char *gecnd_plugins_open_lua(lua_State *L);

// instance
gecnd_t         *gecnd_new(lua_State* L);
gecnd_t         *gecnd_get_root(void);
bool             gecnd_is_root(gecnd_t *gly);
void             gecnd_destroy(gecnd_t *gly);
gecnd_display_t *gecnd_get_display(void);
void             gecnd_set_state(gecnd_t *gly, gecnd_fsm_t new_state);

// configure
void gecnd_set_loop(gecnd_t *gly, void* loop);
void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]);
void gecnd_set_delta(gecnd_t *gly, int16_t ms);
void gecnd_set_flags(gecnd_t *gly, int32_t flags);
void gecnd_set_screensize(gecnd_t *gly, int16_t width, int16_t height);
// status
uint32_t gecnd_get_flags(gecnd_t *gly);
uint32_t gecnd_get_sleep(gecnd_t *gly);
gecnd_fsm_t gecnd_get_state(gecnd_t *gly);
// error
bool gecnd_has_errors(gecnd_t *gly);
const char* gecnd_get_errors(gecnd_t *gly);
void gecnd_add_error(gecnd_t *gly, const char *fmt, ...);
// tick
bool gecnd_update(gecnd_t *gly);
void gecnd_dispatch_key_event(const char *name, bool pressed, int port, void *usr);
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

/* ---- Daemon_FS ---- */

typedef enum {
    GLY_FS_ONE,           /* sync,  first result  — a=char**, b=NULL     */
    GLY_FS_ONE_CB,        /* sync,  first result  — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB,        /* sync,  all results   — a=gamely_fs_cb, b=usr */
    GLY_FS_ONE_CB_ASYNC,  /* async, first result  — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB_ASYNC,  /* async, all results   — a=gamely_fs_cb, b=usr */
} gamely_fs_mode_t;

typedef void (*gamely_fs_cb)     (const char *path, void *usr);
typedef void (*gamely_fs_read_cb)(uint8_t *data, size_t len, void *usr);

void gamely_daemon_fs_start(void *loop);
void gamely_daemon_fs_stop (void);

/* paths[]: dirs or full paths, NULL-terminated; support globs (/mnt/ * / * /roms).
 * files[]: NULL=use paths as-is, ["*"]=list dir, ["name"]=search file.
 * exts[] : NULL=no filter, [".png",".etc1",...]=filter by extension.
 * a/b    : GLY_FS_ONE->(char**,NULL) | others->(gamely_fs_cb,usr).
 * Returns 0 on success (or async started), -1 on error / not found. */
int  gamely_daemon_fs_search(const char      **paths,
                              const char      **files,
                              const char      **exts,
                              gamely_fs_mode_t  mode,
                              void             *a,
                              void             *b);

/* on_done=NULL -> sync (fills *out_data/*out_len, caller frees).
 * on_done!=NULL -> async; delivered via gamely_daemon_fs_tick(). */
int  gamely_daemon_fs_read  (const char        *path,
                              uint8_t          **out_data,
                              size_t            *out_len,
                              gamely_fs_read_cb  on_done,
                              void              *usr);

void gamely_daemon_fs_tick(void);

/* Registers file:// and "" schemas with Daemon_Img.
 * Call after gamely_daemon_fs_start() and gamely_daemon_img_start(). */
void gamely_daemon_io_resolver_start(void);

/* ---- Daemon_DB ---- */

void    gamely_daemon_db_start(void);
void    gamely_daemon_db_stop (void);

int32_t gamely_daemon_db_insert_media(
    const char *name,
    const char *short_id,
    const char *url,
    const char *type,
    const char *url_image
);
void    gamely_daemon_db_delete_media(const char *short_id);
/* Deletes every media row whose `type` matches (NULL/"" = all rows). */
void    gamely_daemon_db_delete_media_by_type(const char *type);

int32_t gamely_daemon_db_insert_blob(
    const uint8_t *data,
    size_t         len,
    const char    *hint
);
void    gamely_daemon_db_delete_blob(int32_t id);

void        gamely_daemon_db_kv_set(const char *key, const char *value);
const char *gamely_daemon_db_kv_get(const char *key);

/* Generic query — parses db://table/field?k=v[&k=v…], returns heap-allocated
 * value cast to the column type (TEXT→char*, INTEGER→int64_t*, BLOB→uint8_t*).
 * *out_len is filled for BLOB; optional for TEXT (strlen). NULL if not found.
 * Caller must free() the returned pointer. */
void *gamely_daemon_db_query_uri(const char *uri, size_t *out_len);

/* Heap-allocated JSON array of media rows whose `type` matches (NULL/"" = all):
 * [{"name":..,"short":..,"url":..,"type":..,"url_image":..}, …]. Empty → "[]".
 * Caller must free(). NULL on error. */
char *gamely_daemon_db_media_json(const char *type);

/* ---- Daemon_Img ---- */

typedef enum {
    GLY_IMG_SEARCHING = 0,
    GLY_IMG_DECODING  = 1,
    GLY_IMG_READY     = 2,
    GLY_IMG_ERROR     = 3,
} gamely_img_state_t;

typedef struct {
    uint8_t         *pixels;
    size_t           len;        /* bytes in `pixels` — backend may rely on this */
    int16_t          w, h;
    GECNDColorFormat color_format;  /* the format the decoder actually produced */
    uint8_t          flags;
} gamely_img_decoded_t;

typedef gamely_img_decoded_t (*gamely_img_decoder_cb)(const uint8_t *data, size_t len);

typedef void (*gamely_img_release_cb)(void *ptr);

typedef void (*gamely_img_upload_cb)(
    int32_t               id,
    void                **backend_data,
    const uint8_t        *data,
    size_t                len,
    int16_t               w,
    int16_t               h,
    GECNDColorFormat      color_format,
    gamely_img_release_cb release
);

typedef struct {
    gamely_img_upload_cb  upload;
    void (*draw)      (int32_t id, void *backend_data, int16_t x, int16_t y);
    void (*unload)    (int32_t id, void *backend_data);
    void (*unload_all)(void);
} gamely_img_backend_t;

typedef void (*gamely_img_on_fetch_cb)(
    const uint8_t *data, size_t len, const char *hint, void *usr
);

typedef void (*gamely_img_schema_cb)(
    const char *url, void *schema_usr,
    gamely_img_on_fetch_cb on_done, void *on_done_usr
);

void gamely_daemon_img_start(void *loop);
void gamely_daemon_img_stop (void);

/* Returns the ID for url. Starts async load on first call.
 * Every URL keeps its ID until an unload is called. */
int32_t            gamely_daemon_img_get_id     (const char *url);
gamely_img_state_t gamely_daemon_img_get_state  (int32_t id);
const char        *gamely_daemon_img_get_error  (int32_t id);
void               gamely_daemon_img_get_mensure(int32_t id, int16_t *w, int16_t *h);
void               gamely_daemon_img_draw       (int32_t id, int16_t x, int16_t y);
void               gamely_daemon_img_unload_id  (int32_t id);
void               gamely_daemon_img_unload_url (const char *url);
void               gamely_daemon_img_unload_all (void);
bool               gamely_daemon_img_has_backend(const char *fmt);
/* True if some registered decoder can turn `from` (e.g. "jpg") into a format
 * that has a backend — i.e. the dispatcher would be able to display it. */
bool               gamely_daemon_img_can_decode (const char *from);
int32_t            gamely_daemon_img_loading_count(void);

/* ---- Web Daemons: include/gdweb.h ---- */

/* ---- Backends ---- */

/* Registers the OpenGL atlas backend for "rgba" with Daemon_Img.
 * Call after gamely_daemon_img_start(). */
void gamely_daemon_img_opengl_register(void);

/* ---- Hypervisor ---- */

void gamely_hypervisor_init(gecnd_t *gly);
void gamely_hypervisor_tick(void);
void gamely_hypervisor_exit(void);

/* ---- Daemon_Input ---- */

typedef void (*gamely_input_key_cb)(const char *name, bool pressed, int port, void *usr);

typedef struct {
    bool (*open)(int port, const char *searchparams);
    void (*close)(int port);
} gamely_input_driver_t;

/* build phase — called by set_toml.c / glfw.c */
void gamely_daemon_input_add_keycode(const char *class_name, const char *key_name, uint32_t hex);

/* register an input source URI (replaces add_source); must be called before tick() */
void gamely_input_add_url(const char *url);

/*
 * gamely_input_add_cb(tag, fn, usr) — unified callback registration.
 * Returns true on success, false if a required class was not found.
 *
 *   "@tick"      fn: void (*)(void)
 *                called at the start of every gamely_daemon_input_tick()
 *
 *   "@code"      fn: gamely_input_key_cb  (name, pressed, port, usr)
 *                called for every resolved key event
 *
 *   "@init"      fn: gamely_input_key_cb  (name, pressed, port, usr)
 *                fired once (on first tick after init) for every key in all
 *                active sources with pressed=false
 *
 *   <classname>  fn: void (*)(const char *name, bool pressed, int port)  [no usr]
 *                called for every key event; marks <classname> in_use
 *
 *   "from:to"    fn: void (*)(uint32_t code, bool pressed, int port)  [no usr]
 *                cross-keymap translation: active class resolves the name,
 *                then "to" class translates name→code; fires only when both
 *                match. "from" constrains which active class triggers (empty =
 *                all). Returns false if "from" or "to" class not registered.
 *                Both classes are marked in_use.
 *
 *   ":to"        same as "from:to" with empty from (wildcard — all actives).
 */
bool gamely_input_add_cb(const char *tag, void *fn, void *usr);

/* free keymap classes not marked in_use; no-op in debug mode */
void gamely_daemon_input_cleanup(void);

/* drivers are opened lazily on first push; close releases all driver handles */
void gamely_daemon_input_close(void);

/* inject from driver threads; port from open(); ttl_ms=0 = no TTL */
void gamely_daemon_input_push     (uint32_t code, bool pressed, uint32_t ttl_ms);
/* inject with explicit port — for service_rc.c; ttl_ms=0 = no TTL */
void gamely_daemon_input_push_name(const char *name, bool pressed, int port, uint32_t ttl_ms);

/* main thread */
void gamely_daemon_input_tick      (void);
void gamely_daemon_input_reset_port(int port);

/* remote input propagator — connects to url and forwards local inputs */
void gamely_daemon_input_remote(const char *url);

/* ---- Daemon_Media ---- */

bool        gamely_daemon_media_background_claim        (void);
void        gamely_daemon_media_background_release      (void);

void        gamely_daemon_media_background_push_yuv420  (const uint8_t *y,
                                                          const uint8_t *u,
                                                          const uint8_t *v,
                                                          int w, int h,
                                                          int y_stride, int uv_stride);
void        gamely_daemon_media_background_push_xrgb8888(const uint8_t *data,
                                                          int w, int h, int pitch);
void        gamely_daemon_media_background_push_rgb565  (const uint8_t *data,
                                                          int w, int h, int pitch);

MediaFrame *gamely_daemon_media_background_get_frame   (void);
bool        gamely_daemon_media_background_check_update(atomic_int *local_counter);

typedef void (*gamely_transmit_cb_t)(const uint8_t *buf, int size, int64_t pts);

void gamely_daemon_media_transmit_callback    (gamely_transmit_cb_t cb);
void gamely_daemon_media_transmit_shutdown    (void);
bool gamely_daemon_media_transmit_is_online   (void);
void gamely_daemon_media_transmit_push        (const uint8_t *rgba, int width, int height);
void gamely_daemon_media_transmit_force_idr   (void);
int  gamely_daemon_media_transmit_get_idr_cache(const uint8_t **out);

typedef void (*gamely_audio_cb_t)(const int16_t *data, size_t frames,
                                   unsigned rate, unsigned channels, void *usr);

void gamely_daemon_media_audio_subscribe(gamely_audio_cb_t cb, void *usr);
void gamely_daemon_media_audio_configure(unsigned rate, unsigned channels);
void gamely_daemon_media_audio_push     (const int16_t *data, size_t frames);

void gamely_daemon_media_init    (void);
void gamely_daemon_media_shutdown(void);

typedef struct {
    typeof(gecnd_lang) *const lang;
    typeof(gecnd_registry) *const registry;
} gecnd_api_t;

typedef struct gecnd_plugin gecnd_plugin_t;
struct gecnd_plugin {
    gecnd_api_t *(*require)(const char *abi);
    bool (*load)(const char *module);
};

#endif
