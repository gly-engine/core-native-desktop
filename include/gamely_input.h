#ifndef GAMELY_INPUT_H
#define GAMELY_INPUT_H

/**
 * @startuml
 * participant "driver\nthread" as D
 * participant "service_io" as IO
 * participant "service_keymap" as KM
 * participant "main\nthread" as M
 * participant "subscriber" as S
 *
 * D  -> IO : push(code, pressed, ttl)
 * IO -> KM : lookup(code) -> name*
 * IO -> IO : enqueue {name*, pressed, port}
 * ...tick()...
 * M  -> IO : tick()
 * IO -> IO : check TTL expiry -> enqueue expired
 * IO -> IO : drain queue
 * IO -> S  : cb(name, pressed, port, usr)
 * @enduml
 */

#include <stdbool.h>
#include <stdint.h>

typedef void (*gamely_input_key_cb)(const char *name, bool pressed, int port, void *usr);

typedef struct {
    bool (*open)(int port, const char *device);
    void (*close)(int port);
} gamely_input_driver_t;

/* build phase — called by set_toml.c */
void gamely_daemon_input_add_class(const char *name);
void gamely_daemon_input_add_keycode(const char *key_name, uint32_t hex);

/* register sources — called once per --input before open() */
void gamely_daemon_input_add_source(const char *uri);

/* activate all registered sources; falls back to void://0 if none */
bool gamely_daemon_input_open(void);
void gamely_daemon_input_close(void);

/* inject from driver threads; port from open(); ttl_ms=0 = no TTL */
void gamely_daemon_input_push(uint32_t code, bool pressed, uint32_t ttl_ms);

/* inject with explicit port — for service_rc.c; ttl_ms=0 = no TTL */
void gamely_daemon_input_push_name(const char *name, bool pressed, int port, uint32_t ttl_ms);

/* main thread */
void gamely_daemon_input_subscribe(gamely_input_key_cb cb, void *usr);
void gamely_daemon_input_tick(void);
void gamely_daemon_input_reset_port(int port);

/* fires cb(name, false, port, usr) for every entry across all active sources */
void gamely_daemon_input_init_keys(gamely_input_key_cb cb, void *usr);

/* remote input propagator — connects to url and forwards local inputs */
void gamely_daemon_input_remote(const char *url);

#endif
