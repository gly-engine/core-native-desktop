# Daemon_Media

## Visão geral

Substitui `lib/Stream_AVlib/` e `lib/Frontend_Api/media.c` por uma daemon com prefixo
`gamely_daemon_media_*`, seguindo o padrão de `Daemon_Inputs` e `Daemon_WebServer`.

Três responsabilidades **completamente independentes**:

| Responsabilidade     | Arquivos                                              | Descrição                                                        |
|----------------------|-------------------------------------------------------|------------------------------------------------------------------|
| **Background buffer**| `service_background.c`                                | Triple buffer compartilhado entre libretro e playback; lido pelo OpenGL |
| **Playback**         | `service_playback.c`, `driver_av_decode.c`            | Decodifica vídeo de URL → background buffer → OpenGL renderiza   |
| **Transmissão**      | `service_transmit.c`, `driver_av_encode.c`            | `glReadPixels` → slot atômico → encode H264/MPEG-TS → broadcast  |
| **FFmpeg loaders**   | `driver_av_dyn.c`, `driver_av_static.c`, `driver_av_stub.c` | Carregam símbolos FFmpeg; compartilhados pelos dois serviços |

`driver_av_stub.c` — usado em builds sem FFmpeg; preenche todos os ponteiros do `av_api`
com stubs que emitem erro em stderr e retornam falha, sem crash.

Os dois caminhos de **escrita** (libretro e service_playback) são mutuamente exclusivos
no background buffer via `background_is_available()` / `background_claim()` /
`background_release()`. A transmissão **não usa** o background buffer — captura
diretamente do framebuffer OpenGL.

---

## Estrutura de arquivos

```
lib/Daemon_Media/
    service_background.c    (ex-set_buffer.c: triple buffer + push functions + ownership)
    service_playback.c      (gerencia VideoStream por canal)
    service_transmit.c      (gerencia clientes HTTP stream, passa TS para cada um)
    driver_av_decode.c      (thread de decode — ex-media.c)
    driver_av_encode.c      (thread de encode H264 + mux MPEG-TS)
    driver_av_dyn.c         (loader dinâmico — ex-open_dynamic.c + símbolos encode)
    driver_av_static.c      (linkagem estática — ex-open_static.c + símbolos encode)
    driver_av_stub.c        (stubs sem FFmpeg)

lib/Frontend_Libretro/
    buffer.c                (removido — SIMD absorvido por service_background)

include/
    gamely_media.h          (header público de toda a daemon)
```

Removidos:

```
lib/Stream_AVlib/               (inteiro)
lib/Frontend_Core/set_buffer.c  (migrado para Daemon_Media/service_background.c)
lib/Frontend_Api/media.c        (substituído por service_playback)
lib/Frontend_Libretro/buffer.c  (SIMD migrado para service_background)
```

---

## `include/gamely_media.h`

```c
#ifndef GAMELY_MEDIA_H
#define GAMELY_MEDIA_H

#include <stdbool.h>
#include <stdint.h>
#include "gebuffer.h"   /* MediaFrame, GECNDColorFormat */

/* -----------------------------------------------------------------------
 * Background buffer — triple buffer compartilhado (libretro ou playback)
 * --------------------------------------------------------------------- */

/* Ownership — apenas um produtor por vez */
bool gamely_daemon_media_background_is_available (void);
bool gamely_daemon_media_background_claim        (void);
void gamely_daemon_media_background_release      (void);

/* Push tipados — encapsulam resize + conversão SIMD + swap */
void gamely_daemon_media_background_push_yuv420  (const uint8_t *y,
                                                   const uint8_t *u,
                                                   const uint8_t *v,
                                                   int w, int h,
                                                   int y_stride, int uv_stride);
void gamely_daemon_media_background_push_xrgb8888(const uint8_t *data,
                                                   int w, int h, int pitch);
void gamely_daemon_media_background_push_rgb565  (const uint8_t *data,
                                                   int w, int h, int pitch);

/* Leitura — chamado pelo backend OpenGL */
MediaFrame *gamely_daemon_media_background_get_frame(void);

/* -----------------------------------------------------------------------
 * Playback — reprodução de vídeo por canal
 * --------------------------------------------------------------------- */
void gamely_daemon_media_source  (uint8_t channel, const char *url);
void gamely_daemon_media_play    (uint8_t channel);
void gamely_daemon_media_pause   (uint8_t channel);
void gamely_daemon_media_stop    (uint8_t channel);
void gamely_daemon_media_position(uint8_t channel,
                                   int16_t x, int16_t y,
                                   int16_t w, int16_t h);

/* -----------------------------------------------------------------------
 * Transmissão — H264/MPEG-TS → clientes HTTP /stream
 * --------------------------------------------------------------------- */

/* Chamado no encoder thread a cada pacote MPEG-TS pronto */
typedef void (*gamely_transmit_cb_t)(const uint8_t *buf, int size, int64_t pts);

/* Registra callback e marca encoder como online (init real é lazy no 1º push) */
void gamely_daemon_media_transmit_callback (gamely_transmit_cb_t cb);

/* Para o encoder e limpa o callback */
void gamely_daemon_media_transmit_shutdown (void);

/* True enquanto encoder está online — usado pelo OpenGL para fazer glReadPixels */
bool gamely_daemon_media_transmit_is_online(void);

/* Envia frame RGBA ao encoder; só age se is_online(); init lazy se dims mudaram */
void gamely_daemon_media_transmit_push     (const uint8_t *rgba, int width, int height);

/* -----------------------------------------------------------------------
 * Ciclo de vida
 * --------------------------------------------------------------------- */
void gamely_daemon_media_init    (void);
void gamely_daemon_media_shutdown(void);

#endif
```

`gebuffer.h` passa a definir **apenas** `MediaFrame` e `GECNDColorFormat` — sem
funções públicas. As funções do triple buffer ficam internas a `service_background.c`.

---

## `service_background.c` — triple buffer com ownership

### Ownership

Um único dono por vez. `claim()` retorna `false` se já reivindicado.

```c
static atomic_int g_owner = 0;   /* 0=livre, 1=ocupado */

bool gamely_daemon_media_background_is_available(void) {
    return atomic_load(&g_owner) == 0;
}

bool gamely_daemon_media_background_claim(void) {
    int expected = 0;
    return atomic_compare_exchange_strong(&g_owner, &expected, 1);
}

void gamely_daemon_media_background_release(void) {
    atomic_store(&g_owner, 0);
}
```

`service_playback` chama `claim()` em `gamely_daemon_media_source()` e `release()`
em `gamely_daemon_media_stop()`.

`open_libretro.c` chama `claim()` ao iniciar o core e `release()` ao fechar.

### Triple buffer

Implementação idêntica ao `set_buffer.c` atual:
- `frames[3]`, `front_idx`, `back_idx`, `mid_idx`, `mid_dirty` — todos `atomic_int`
- `gecnd_buffer_swap()`: `atomic_exchange(mid_idx, back_idx)` + `mid_dirty=1`
- `gamely_daemon_media_background_get_frame()`: consome mid se dirty, retorna front

### Push functions + SIMD

As funções SIMD `libretro_copy_xrgb8888` e `libretro_copy_rgb565` migram de
`Frontend_Libretro/buffer.c` para `service_background.c` como estáticas.

Cada push:
1. `gecnd_buffer_resize_internal(w, h, format)` se necessário
2. `gecnd_buffer_get_back_internal()` → copia/converte
3. `atomic_store(&f->ready, true)` + `gecnd_buffer_swap_internal()`

---

## Simplificação dos callers

**`open_libretro.c`**:
```c
/* ao abrir o core */
if (!gamely_daemon_media_background_claim()) { /* erro: já em uso */ }

/* core_video_refresh */
if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
    gamely_daemon_media_background_push_xrgb8888(data, w, h, (int)pitch);
else
    gamely_daemon_media_background_push_rgb565(data, w, h, (int)pitch);

/* ao fechar o core */
gamely_daemon_media_background_release();
```

**`driver_av_decode.c`** (dentro do decode loop):
```c
gamely_daemon_media_background_push_yuv420(
    vfrm->data[0], vfrm->data[1], vfrm->data[2],
    vfrm->width, vfrm->height,
    vfrm->linesize[0], vfrm->linesize[1]);
```

**Backend OpenGL** (ex-`gecnd_get_background_frame`):
```c
MediaFrame *f = gamely_daemon_media_background_get_frame();
```

---

## Mudanças em componentes existentes

### `include/gamely_webserver.h`

**Adicionar:**

```c
typedef void (*gly_stream_cb_t)(uint32_t conn_id, bool connected);
void gamely_daemon_webserver_route_stream(const char *path, gly_stream_cb_t cb);
void gamely_daemon_webserver_route_proxy (const char *from, const char *to);

/* Thread-safe: acorda o event-loop para drenar chunks pendentes de streams.
 * Chamado do encoder thread depois de escrever no ring buffer de cada sessão. */
void gamely_daemon_webserver_notify(void);
```

**Remover:**

```c
void gamely_daemon_webserver_proxy_http(const char *from, const char *to);
void gamely_daemon_webserver_proxy_ws (const char *from, const char *to);
```

**Semântica de `gamely_http_respond` em rotas stream:**

| Chamada                           | Efeito                                                                         |
|-----------------------------------|--------------------------------------------------------------------------------|
| Primeira (qualquer `status`)      | Envia `200 OK`, `Transfer-Encoding: chunked`, `Content-Type: <type>`           |
| Subsequentes com `body_len > 0`   | Lock → escreve no ring buffer da sessão → unlock → `gamely_daemon_webserver_notify()` |
| `body == NULL` ou `body_len == 0` | Envia chunk final `0\r\n\r\n` e fecha a conexão                                |

> `gamely_http_respond` é thread-safe para conn_ids de rotas stream: o write ao ring
> buffer é protegido por `uv_mutex_t` e a notificação usa `uv_async_send`.

### `lib/Daemon_WebServer/driver_warmcat.c`

Adicionar `ROUTE_STREAM` ao enum `route_type_t`.

```c
typedef struct {
    gly_conn_id_t  conn_id;
    bool           headers_sent;
    unsigned char  ring[STREAM_RING_SIZE];
    unsigned int   write_pos, read_pos;
    uv_mutex_t     lock;
} stream_session_t;
```

- `gamely_http_respond` em conn_id stream (pode vir de qualquer thread):
  `lock` → escreve no ring (descarta dados antigos se cheio) → `unlock` → `gamely_daemon_webserver_notify()`
- `gamely_daemon_webserver_notify()`: chama `uv_async_send(&g_async)` (thread-safe)
- `g_async` callback (event-loop): `lws_callback_on_writable_all_protocol(ctx, http_protocol)`
- `LWS_CALLBACK_HTTP_WRITEABLE`: se `headers_sent=false` envia headers chunked; depois drena ring com `lws_write(LWS_WRITE_HTTP)`; re-agenda se ainda há dados
- `LWS_CALLBACK_CLOSED_HTTP`: `uv_mutex_destroy` + invoca `stream_cb(conn_id, false)`

### `lib/Backend_OpenGL/pipeline/core.c`

```c
/* ge_pipeline_init */
g_readpixels_buf = malloc(max_w * max_h * 4);

/* ge_pipeline_flush(), após flush_primitives */
if (gamely_daemon_media_transmit_is_online()) {
    GLBackendState *s = geogl_get_state();
    glReadPixels(0, 0, s->window_width, s->window_height,
                 GL_RGBA, GL_UNSIGNED_BYTE, g_readpixels_buf);
    gamely_daemon_media_transmit_push(g_readpixels_buf,
                                      s->window_width, s->window_height);
}

/* ge_pipeline_terminate */
free(g_readpixels_buf);
```

Uso de `gamely_daemon_media_background_get_frame()` substitui `gecnd_get_background_frame()`.

### `lib/Frontend_Api/media.c`

Substituir `native_media_*` por `gamely_daemon_media_*`. Assinaturas Lua inalteradas.

---

## `include/gemedia.h` — adições ao `av_api`

```c
/* libavcodec — encode */
const AVCodec*  (*avcodec_find_encoder)(enum AVCodecID id);
int             (*avcodec_send_frame)(AVCodecContext *avctx, const AVFrame *frame);
int             (*avcodec_receive_packet)(AVCodecContext *avctx, AVPacket *avpkt);

/* libavformat — mux MPEG-TS */
int       (*avformat_alloc_output_context2)(AVFormatContext **ctx,
               const AVOutputFormat *oformat,
               const char *format_name, const char *filename);
AVStream* (*avformat_new_stream)(AVFormatContext *s, const AVCodec *c);
int       (*avformat_write_header)(AVFormatContext *s, AVDictionary **options);
int       (*av_interleaved_write_frame)(AVFormatContext *s, AVPacket *pkt);
int       (*av_write_trailer)(AVFormatContext *s);
void      (*avformat_free_context)(AVFormatContext *s);
int       (*avio_open_dyn_buf)(AVIOContext **s);
int       (*avio_close_dyn_buf)(AVIOContext *s, uint8_t **pbuffer);
AVIOContext* (*avio_alloc_context)(unsigned char *buffer, int buffer_size,
               int write_flag, void *opaque,
               int (*read_packet)(void*, uint8_t*, int),
               int (*write_packet)(void*, uint8_t*, int),
               int64_t (*seek)(void*, int64_t, int));
```

---

## `driver_av_encode.c` — encoder H264 + broadcast MPEG-TS

### Estado interno

```
slot atômico:   { uint8_t *rgba; int width, height; uint64_t seq; }
mutex + condvar: produtor acorda consumidor sem spin
encoder thread: uv_thread_t
callback:       encode_ts_cb_t on_ts_ready  (registrada por service_transmit)
```

### Produtor — render thread (`gamely_daemon_media_push_frame`)

```
1. copia rgba para slot atômico
2. incrementa seq (atomic)
3. pthread_cond_signal → retorna imediatamente
```

### Consumidor — encoder thread

```
loop:
    1. pthread_cond_wait
    2. lê slot atômico (seq, rgba, w, h)
    3. RGBA → YUV420P (conversão inline)
    4. avcodec_send_frame → avcodec_receive_packet (H264, ultrafast/zerolatency)
    5. avio_open_dyn_buf → av_interleaved_write_frame → avio_close_dyn_buf
    6. on_ts_ready(ts_buf, ts_len)
    7. av_free(ts_buf)
```

---

## `service_transmit.c`

Responsabilidades: ciclo de vida do encoder, API pública de transmissão.
A gestão de clientes HTTP e PTS fica em `service_stream.c` (Daemon_WebServer).

```c
static gamely_transmit_cb_t g_cb     = NULL;
static atomic_bool           g_online = false;
static int                   g_enc_w  = 0, g_enc_h = 0;

/* Chamado pelo encoder thread (driver_av_encode) a cada pacote MPEG-TS.
 * pts = g_pts do encoder (monotonicamente crescente, unidade: frames @ fps) */
static void on_ts_ready(const uint8_t *buf, int size) {
    if (g_cb) g_cb(buf, size, g_pts_last);   /* passa PTS para service_stream */
}

/* ── API pública ──────────────────────────────────────────────────────── */

/* Registra callback e marca encoder como online.
 * O encoder real é inicializado de forma lazy no primeiro transmit_push(). */
void gamely_daemon_media_transmit_callback(gamely_transmit_cb_t cb) {
    g_cb = cb;
    atomic_store(&g_online, true);
}

/* Para encoder e limpa callback. */
void gamely_daemon_media_transmit_shutdown(void) {
    atomic_store(&g_online, false);
    encode_shutdown();
    g_enc_w = g_enc_h = 0;
    g_cb = NULL;
}

bool gamely_daemon_media_transmit_is_online(void) {
    return atomic_load(&g_online);
}

/* Chamado pelo backend OpenGL após glReadPixels.
 * Se dimensões mudaram, reinicia o encoder com as novas dimensões. */
void gamely_daemon_media_transmit_push(const uint8_t *rgba, int w, int h) {
    if (!atomic_load(&g_online)) return;
    if (w != g_enc_w || h != g_enc_h) {
        encode_shutdown();
        g_enc_w = w; g_enc_h = h;
        encode_init(w, h, 30, on_ts_ready);
    }
    encode_push(rgba, w, h);
}
```

> **Nota sobre PTS**: `g_pts_last` é a variável `g_pts` de `driver_av_encode.c`,
> exposta como `int64_t encode_pts(void)` ou passada junto com o buffer via
> extensão da assinatura de `encode_ts_cb`: `void (*)(const uint8_t*, int, int64_t)`.

---

## `service_stream.c` — `lib/Daemon_WebServer/`

Responsabilidades: rota HTTP `/stream`, ciclo de vida por cliente, deduplicação por PTS.

```
#define MAX_STREAM_CLIENTS 8

typedef struct {
    gly_conn_id_t conn_id;
    int64_t       last_pts;   /* -1 = nunca recebeu */
    bool          active;
} StreamSlot;
```

### Ciclo de vida do encoder via contagem de clientes

```c
static StreamSlot g_slots[MAX_STREAM_CLIENTS];
static int        g_count = 0;            /* clientes ativos */
static uv_mutex_t g_lock;

/* Último pacote TS recebido — enviado imediatamente a novos clientes */
static uint8_t  *g_last_buf  = NULL;
static int       g_last_size = 0;
static int64_t   g_last_pts  = -1;

static void on_stream_client(gly_conn_id_t id, bool connected) {
    uv_mutex_lock(&g_lock);
    if (connected) {
        /* adiciona slot */
        for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
            if (g_slots[i].active) continue;
            g_slots[i] = (StreamSlot){ .conn_id=id, .last_pts=-1, .active=true };
            g_count++;
            break;
        }
        if (g_count == 1)
            gamely_daemon_media_transmit_callback(on_ts_packet);   /* start encoder */

        /* envia último frame cacheado ao novo cliente */
        if (g_last_buf && g_last_size > 0)
            gamely_http_respond(id, 200, "video/mp2t",
                                (const char *)g_last_buf, (size_t)g_last_size);
    } else {
        /* remove slot */
        for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
            if (!g_slots[i].active || g_slots[i].conn_id != id) continue;
            g_slots[i].active = false;
            g_count--;
            break;
        }
        if (g_count == 0)
            gamely_daemon_media_transmit_shutdown();               /* stop encoder */
    }
    uv_mutex_unlock(&g_lock);
}
```

### Callback do encoder — deduplicação por PTS

```c
/* Chamado no encoder thread para cada pacote MPEG-TS */
static void on_ts_packet(const uint8_t *buf, int size, int64_t pts) {
    uv_mutex_lock(&g_lock);

    /* Atualiza cache do último frame */
    if (size > g_last_size) {
        free(g_last_buf);
        g_last_buf = malloc((size_t)size);
    }
    memcpy(g_last_buf, buf, (size_t)size);
    g_last_size = size;
    g_last_pts  = pts;

    /* Envia a cada cliente que ainda não recebeu este PTS */
    for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
        if (!g_slots[i].active)           continue;
        if (g_slots[i].last_pts == pts)   continue;   /* já enviado */
        gamely_http_respond(g_slots[i].conn_id, 200, "video/mp2t",
                            (const char *)buf, (size_t)size);
        g_slots[i].last_pts = pts;
    }

    uv_mutex_unlock(&g_lock);
}
```

> `gamely_http_respond` para conn_ids de rota stream é thread-safe:
> escreve no ring da sessão LWS com `uv_mutex_t` e usa `uv_async_send` para
> notificar o event-loop (ver `driver_warmcat.c`).

### Registro

```c
void gamely_service_stream_register(void) {
    uv_mutex_init(&g_lock);
    gamely_daemon_webserver_route_stream("/stream", on_stream_client);
}
```

Chamado em `update.c` junto com `gamely_service_rc_register()`.

---

## `service_playback.c`

Chama `gamely_daemon_media_background_claim()` em `source()` e
`gamely_daemon_media_background_release()` em `stop()`.
Usa `gamely_daemon_media_background_push_yuv420()` via `driver_av_decode`.

---

## Diagramas de fluxo

### Transmissão (OpenGL → /stream)

```
ge_pipeline_flush()()  [render thread]
    └── transmit_is_online()?
            └── glReadPixels → transmit_push(rgba, w, h)
                        │  [retorna imediatamente]
               [encoder thread acorda via uv_cond]
                        │
               RGBA → YUV420P (inline)
                        │
               H264 (ultrafast/zerolatency) → MPEG-TS (avio_dyn_buf)
                        │
               on_ts_ready(buf, size)  →  service_transmit
                        │
               on_ts_packet(buf, size, pts)  →  service_stream
                        │
          ┌─────────────┴─────────────┐
      slot[0]                     slot[N]
   last_pts != pts?            last_pts != pts?
  gamely_http_respond         gamely_http_respond   [thread-safe]
          │                            │
  uv_mutex + ring              uv_mutex + ring      [stream_session_t]
          │                            │
   uv_async_send               uv_async_send        [notify event-loop]
          │                            │
   [event-loop]                [event-loop]
   lws_write HTTP              lws_write HTTP
          │                            │
    ffplay/cliente              ffplay/cliente

Início: 1º cliente → on_stream_client(id,true) → transmit_callback(on_ts_packet)
Fim:    0 clientes → on_stream_client(id,false) → transmit_shutdown()
```

### Playback de vídeo (background buffer)

```
driver_av_decode thread
    └── avcodec_receive_frame
    └── background_push_yuv420(...)  [triple buffer, atomic]
                │
    [render thread]
    └── background_get_frame()
    └── OpenGL renderiza frame de vídeo
```

### Libretro (background buffer)

```
libretro core thread
    └── core_video_refresh
    └── background_push_xrgb8888(...)  [SIMD + triple buffer, atomic]
                │
    [render thread]
    └── background_get_frame()
    └── OpenGL renderiza frame do core
                │
    ge_pipeline_flush()()
    └── transmit_is_online()? → glReadPixels → transmit_push(...)
    └── [encoder captura o que o OpenGL renderizou]
```

---

## Checklist de implementação

### Background buffer (pré-requisito dos demais)
- [ ] `include/gebuffer.h` — apenas `MediaFrame` e `GECNDColorFormat`; remover funções públicas
- [ ] `lib/Daemon_Media/service_background.c` — triple buffer + ownership + push functions + SIMD de libretro/buffer.c
- [ ] `lib/Frontend_Libretro/open_libretro.c` — `claim` + `push_xrgb8888`/`push_rgb565` + `release`
- [ ] `lib/Frontend_Libretro/buffer.c` — remover
- [ ] `lib/Frontend_Core/set_buffer.c` — remover (migrado)
- [ ] Todos os usos de `gecnd_get_background_frame()` → `background_get_frame()`

### WebServer
- [ ] `include/gamely_webserver.h` — `route_stream`, `route_proxy`, `notify`, remover `proxy_http/ws`
- [ ] `lib/Daemon_WebServer/driver_warmcat.c` — `ROUTE_STREAM`, `stream_session_t` (ring + `uv_mutex_t`), `uv_async_t` notify
- [ ] `lib/Daemon_WebServer/service_stream.c` — rota `/stream`, gestão de clientes, cache último frame, deduplicação PTS
- [ ] `lib/Frontend_Core/update.c` — `gamely_service_stream_register()`

### Backend OpenGL
- [ ] `lib/Backend_OpenGL/pipeline/core.c` — `transmit_is_online` + `glReadPixels` + `transmit_push`; `background_get_frame()`

### Daemon_Media
- [ ] `include/gamely_media.h`
- [ ] `include/gemedia.h` — símbolos de encode no `av_api`
- [ ] `lib/Daemon_Media/service_playback.c`
- [ ] `lib/Daemon_Media/service_transmit.c` — `transmit_callback/shutdown/is_online/push`; `encode_ts_cb` com PTS
- [ ] `lib/Daemon_Media/driver_av_decode.c`
- [ ] `lib/Daemon_Media/driver_av_dyn.c`
- [ ] `lib/Daemon_Media/driver_av_static.c`
- [ ] `lib/Daemon_Media/driver_av_stub.c`
- [ ] `lib/Daemon_Media/driver_av_encode.c`
- [ ] `lib/Frontend_Api/media.c` — `native_media_*` → `gamely_daemon_media_*`
