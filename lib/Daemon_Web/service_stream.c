
#include <stdio.h>

#include "gecnd.h"
#include "gdweb.h"


#define MAX_SLOTS 8

static struct {
    gdweb_id_t conn_id;
    int          active;
} g_slots[MAX_SLOTS];

static int g_count = 0;

/* -----------------------------------------------------------------------
 * on_ts_packet — chamado pelo encoder, mesma thread do loop.
 * Distribui os dados TS para cada cliente ativo pelo seu conn_id.
 * ---------------------------------------------------------------------- */
static void on_ts_packet(const uint8_t *buf, int size, int64_t pts)
{
    (void)pts;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_slots[i].active) continue;
        gdweb_control_server()->send(g_slots[i].conn_id, (const char *)buf, (size_t)size);
    }
}

/* -----------------------------------------------------------------------
 * on_stream_client — chamado pelo driver HTTP ao conectar/desconectar.
 * Ao conectar: envia IDR cache para início imediato ou força IDR.
 * ---------------------------------------------------------------------- */
static void service_stream_client_cb(gdweb_id_t conn_id, bool connected)
{
    if (connected) {
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (g_slots[i].active) continue;
            g_slots[i].conn_id = conn_id;
            g_slots[i].active  = 1;
            g_count++;

            printf("[stream] cliente %u conectado (total %d)\n", conn_id, g_count);

            if (g_count == 1) {
                printf("[stream] primeiro cliente — ativando encoder\n");
                gamely_daemon_media_transmit_callback(on_ts_packet);
            }

            /* envia IDR cache para que o player comece a tocar imediatamente,
             * sem esperar pelo próximo keyframe (GOP = fps/2 = até 0.5s) */
            const uint8_t *idr = NULL;
            int idr_len = gamely_daemon_media_transmit_get_idr_cache(&idr);
            if (idr_len > 0) {
                gdweb_control_server()->send(conn_id, (const char *)idr, (size_t)idr_len);
            } else {
                /* stream ainda sem IDR — força keyframe no próximo frame */
                gamely_daemon_media_transmit_force_idr();
            }
            return;
        }
        fprintf(stderr, "[stream] MAX_SLOTS atingido, cliente %u rejeitado\n", conn_id);
    } else {
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (!g_slots[i].active || g_slots[i].conn_id != conn_id) continue;
            g_slots[i].active  = 0;
            g_slots[i].conn_id = 0;
            g_count--;

            printf("[stream] cliente %u desconectado (total %d)\n", conn_id, g_count);

            if (g_count == 0)
                gamely_daemon_media_transmit_shutdown();
            return;
        }
    }
}

__attribute__((constructor))
static void register_stream_routes(void)
{
    gecnd_registry("set", "web_stream_route:stream", (void *)service_stream_client_cb, NULL);
}

