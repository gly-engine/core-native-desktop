#include "gamely_media.h"
#include <stdio.h>

#include "gecnd.h"

extern void gamely_webserver_stream_write_client(gly_req_id_t, const uint8_t *, int);

#define MAX_SLOTS 8

static struct {
    gly_req_id_t conn_id;
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
        gamely_webserver_stream_write_client(g_slots[i].conn_id, buf, size);
    }
}

/* -----------------------------------------------------------------------
 * on_stream_client — chamado pelo driver HTTP ao conectar/desconectar.
 * Ao conectar: envia IDR cache para início imediato ou força IDR.
 * ---------------------------------------------------------------------- */
void service_stream_client_cb(gly_req_id_t conn_id, bool connected)
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
                gamely_webserver_stream_write_client(conn_id, idr, idr_len);
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

