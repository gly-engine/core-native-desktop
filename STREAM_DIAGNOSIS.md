# Diagnóstico e Plano de Correção: Stream MPEG-TS / HTTP Chunked (V2)

Este documento detalha os problemas identificados no sistema de streaming e as modificações necessárias para estabilizar a reprodução no `ffplay` e outros players.

## 1. Problemas Identificados

### A. Buffering Excessivo no Encoder (`driver_av_encode.c`) - **CRÍTICO**
*   **Problema:** O `AVFormatContext` está configurado apenas com `AVFMT_FLAG_CUSTOM_IO`. Sem a flag `AVFMT_FLAG_FLUSH_PACKETS`, o FFmpeg retém os pacotes no buffer `io_buf` de 4096 bytes até que ele esteja cheio.
*   **Sintoma:** O player recebe o IDR inicial (que é grande e enche o buffer), toca os primeiros 3 frames e para, pois os P-frames seguintes são pequenos e ficam "presos" no buffer aguardando o próximo flush. O LWS detecta o ring buffer vazio e pode fechar a conexão por inatividade.

### B. Fragmentação de Pacotes MPEG-TS (`driver_warmcat.c`)
*   **Problema:** O `CHUNK_MAX` atual é 4048 (desalinhado). Deve ser **2632** (14 * 188 bytes), garantindo que cada chunk HTTP contenha um número inteiro de pacotes MPEG-TS.
*   **Sintoma:** Quebra do sync byte (0x47) do MPEG-TS. O player descarta o pacote fragmentado, causando artefatos ou falha total na decodificação.

### C. Conflito de Protocolo HTTP Chunked (`driver_warmcat.c`)
*   **Problema:** O driver está injetando manualmente os tamanhos dos chunks em hexadecimal (`%x\r\n`). O `libwebsockets` já realiza esse gerenciamento quando o header `transfer-encoding: chunked` está presente e usamos `LWS_WRITE_HTTP`.
*   **Sintoma:** "Double chunking", resultando em dados corrompidos para o cliente HTTP.

### D. Capacidade do Ring Buffer
*   **Problema:** 512KB é muito pouco para manter um fluxo constante de vídeo se o player tiver flutuações de rede ou o encoder gerar picos de bitrate.

---

## 2. Modificações Necessárias

### Arquivo: `lib/Daemon_Media/driver_av_encode.c`
1.  **Adicionar Flags de Flush:** 
    ```c
    g_fmt->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_FLUSH_PACKETS;
    ```
2.  **Forçar Flush após Envio de Frame:** Chamar `avio_flush(g_fmt->pb)` após o `av_interleaved_write_frame` para garantir que o pacote saia imediatamente para o servidor web.

### Arquivo: `lib/Daemon_WebServer/driver_warmcat.c`
1.  **Ajustar Macros:**
    ```c
    #define CHUNK_MAX 2632
    #define STREAM_RING (1 << 21) // 2MB
    ```
2.  **Simplificar Escrita HTTP:** Remover a formatação manual do chunk e enviar os dados brutos do ring buffer via `lws_write` com `LWS_WRITE_HTTP`.
3.  **Garantir Alinhamento no `ring_read`:** Garantir que o valor retornado seja sempre múltiplo de 188.

---

## 3. Plano de Execução

1.  **Ato 1:** Corrigir `driver_av_encode.c` (Flags e `avio_flush`).
2.  **Ato 2:** Corrigir `driver_warmcat.c` (Macros, Ring Alinhado e Remoção do Chunk manual).
3.  **Ato 3:** Testar com `ffplay -i http://localhost:PORT/stream -fflags nobuffer -flags low_delay`.
