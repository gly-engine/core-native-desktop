#include "gamely_webserver.h"
#include "gamely_media.h"
#include <stdatomic.h>
#include <stdio.h>

/* interno: definido em driver_warmcat.c */
extern void gamaly_webserver_stream_update(const uint8_t *buf, int size, int64_t pts);

#define MAX_SLOTS 8

typedef struct {
    gly_conn_id_t conn_id;
    atomic_bool   active;
} StreamSlot;

static StreamSlot g_slots[MAX_SLOTS];
static atomic_int g_count = 0;

/* callback do encoder — thread do encoder */
static void on_ts_packet(const uint8_t *buf, int size, int64_t pts)
{
    gamaly_webserver_stream_update(buf, size, pts);
}

/* callback do webserver — thread do LWS/libuv */
static void on_stream_client(gly_conn_id_t conn_id, bool connected)
{
    if (connected) {
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (atomic_load(&g_slots[i].active)) continue;
            g_slots[i].conn_id = conn_id;
            atomic_store(&g_slots[i].active, true);

            int prev = atomic_fetch_add(&g_count, 1);
            printf("[stream] cliente %u conectado (count %d→%d)\n",
                   conn_id, prev, prev + 1);
            if (prev == 0) {
                printf("[stream] primeiro cliente — ativando encoder\n");
                gamely_daemon_media_transmit_callback(on_ts_packet);
            }
            return;
        }
        fprintf(stderr, "[stream] MAX_SLOTS atingido, cliente %u rejeitado\n",
                conn_id);
    } else {
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (!atomic_load(&g_slots[i].active)) continue;
            if (g_slots[i].conn_id != conn_id) continue;
            atomic_store(&g_slots[i].active, false);

            if (atomic_fetch_sub(&g_count, 1) == 1)
                gamely_daemon_media_transmit_shutdown();
            return;
        }
    }
}

void gamaly_service_stream_register(void)
{
    gamaly_daemon_webserver_route_stream("/stream", on_stream_client);
}
