# Design: Daemon_Media — Schema-based Player Registry

Revisão 3 — 2026-04-22

---

## Objetivo

Refatorar `service_playback.c` para seguir o mesmo padrão de registro por schema que
`service_img.c`. Em vez de acoplar diretamente ao FFmpeg, o serviço despacha para o
player registrado que melhor casa com o schema da URL.

Junto disso:
- `lib/Frontend_Libretro/` → `plugin/libretro/` (registrado como schema `"libretro"`)
- `driver_av_decode.c` → registrado para os schemas `""`, `file`, `http`, `https`,
  `rtsp`, `rtmp`, `udp`
- Suporte a schemas compostos (`libretro+pcsx_rearmed`, `http+h266`, etc.)
- Sistema de prioridade: mais específico (mais tokens) ganha; empate: último registrado
- **CMakeLists.txt não será modificado** — usuário cuida disso

---

## Schema composto — tokenização e matching

### Formato de URL

```
<token1>+<token2>+...+<tokenN>://<path>?key=val
```

Exemplos:

| URL | Tokens extraídos |
|-----|-----------------|
| `http://stream.example.com/live` | `{"http"}` |
| `rtsp://camera/stream` | `{"rtsp"}` |
| `libretro+pcsx_rearmed://pepsiman.iso` | `{"libretro", "pcsx_rearmed"}` |
| `pcsx_rearmed+libretro://pepsiman.iso` | `{"pcsx_rearmed", "libretro"}` |
| `http+libretro+tic80://tic80.com/hash/foo.tic` | `{"http", "libretro", "tic80"}` |

A ordem dos tokens no schema **não importa** para matching — é comparação de conjuntos.

### Wildcard `?`

`?` no schema registrado significa "exatamente um token extra obrigatório". A contagem
de extras (tokens da URL que não estão nos named) deve ser **exatamente igual** ao
número de `?` no schema registrado. Se o número de tokens extras for diferente, não casa.

Isso torna o core sempre **explícito** — `libretro://rom` sem core token não casa com
nada e é rejeitado.

### Algoritmo de seleção de player

1. Extrair tokens do schema da URL (split em `+` antes de `://`).
2. Para cada player registrado:
   - Todos os named tokens devem estar presentes nos tokens da URL.
   - `(tokens_url - named_count) == wild_n` (contagem de extras deve ser exata).
3. Entre os candidatos, eleger o de **maior `named_n`** (mais específico).
4. Empate em `named_n` → **último registrado** vence.

Exemplos de seleção:

| URL tokens | Schema registrado | Casa? | Score |
|-----------|-------------------|-------|-------|
| `{http}` | `"http"` named=1 wild=0 | ✓ extras=0==0 | 1 |
| `{http}` | `"http+h266"` named=2 wild=0 | ✗ h266 ausente | — |
| `{http,h266}` | `"http"` named=1 wild=0 | ✗ extras=1≠0 | — |
| `{http,h266}` | `"http+h266"` named=2 wild=0 | ✓ extras=0==0 | 2 |
| `{libretro,pcsx_rearmed}` | `"libretro+?"` named=1 wild=1 | ✓ extras=1==1 | 1 |
| `{http,libretro,tic80}` | `"libretro+?"` named=1 wild=1 | ✗ extras=2≠1 | — |
| `{http,libretro,tic80}` | `"libretro+http+?"` named=2 wild=1 | ✓ extras=1==1 | 2 |

---

## API do player

```c
typedef struct {
    /* Inicia reprodução no canal. url é a URL completa incluindo schema. */
    void (*start)(uint8_t channel, const char *url, void *usr);
    /* Para reprodução e libera recursos do canal. */
    void (*stop) (uint8_t channel, void *usr);
    /* Chamado por service_playback_tick() a cada frame. NULL = não precisa de pump.
     * FFmpeg usa thread própria → tick=NULL. Libretro usa game loop → tick=retro_run. */
    void (*tick) (uint8_t channel, void *usr);
    /* Pausa/retoma. NULL = service ignora play/pause para esse player. */
    void (*pause)(uint8_t channel, void *usr);
    void (*play) (uint8_t channel, void *usr);
} gamely_media_player_t;
```

Funções a adicionar em `gamely_media.h`:

```c
void gamely_daemon_media_register_player(
    const char                  *schema,
    const gamely_media_player_t *cbs,
    void                        *usr
);

/* Chamado pelo game loop a cada frame — despacha tick() para canais ativos. */
void gamely_daemon_media_playback_tick(void);
```

---

## Ciclo de vida de um canal

O número de canal é um stub para uso futuro; na prática um único canal estará ativo.

```
IDLE ──source()──> LOADING ──start() retorna──> PLAYING
                                                    │
                             PAUSED <──pause()──────┤
                             PAUSED ───play()──────> PLAYING
                                                    │
                             IDLE   <──stop()────────┘
                             IDLE   <──erro interno───┘
```

| Estado | Significado |
|--------|-------------|
| IDLE | sem mídia carregada |
| LOADING | `start()` chamado; aguardando primeiro frame |
| PLAYING | frames sendo enviados ao background buffer; `tick()` ativo |
| PAUSED | `pause()` ativo; `tick()` não é despachado |
| ERROR | player reportou falha; canal volta a IDLE no próximo `source()` |

---

## Arquivos resultantes

### `lib/Daemon_Media/service_playback.c` (reescrito)

- Registry de players (`PLAYER_CAP` = 32).
- `gamely_daemon_media_playback_source(channel, url)` — tokeniza o schema, seleciona
  player (algoritmo acima), para o player anterior se houver, chama `player.start()`.
- `gamely_daemon_media_playback_tick()` — itera canais PLAYING/LOADING com tick≠NULL
  e invoca `player.tick(channel)`.
- `gamely_daemon_media_playback_play/pause/stop()` — delegam para o player ativo.
- Protótipos dos register functions dos drivers declarados diretamente aqui, sem headers
  separados.

### `lib/Daemon_Media/driver_av_player.c` (renome de `driver_av_decode.c`)

Implementa `av_player_t` com thread FFmpeg interna (`tick = NULL`):

```c
void gamely_daemon_media_av_player_register(void) {
    gamely_daemon_media_register_player(""     , &av_player, NULL);
    gamely_daemon_media_register_player("file" , &av_player, NULL);
    gamely_daemon_media_register_player("http" , &av_player, NULL);
    gamely_daemon_media_register_player("https", &av_player, NULL);
    gamely_daemon_media_register_player("rtsp" , &av_player, NULL);
    gamely_daemon_media_register_player("rtmp" , &av_player, NULL);
    gamely_daemon_media_register_player("udp"  , &av_player, NULL);
}
```

### `plugin/libretro/open_libretro.c` (movido de `lib/Frontend_Libretro/`)

`open_libretro_gecnd()` registra os dois players libretro e faz o setup de schemas.
O core name é extraído dos tokens da URL que **não são** `"libretro"` e não são tokens
de transporte conhecidos (`http`, `https`, `file`). Princípio: apenas **um** token
extra por URL — usar dois cores simultaneamente não é suportado.

#### Player 1 — `"libretro"` (arquivo local via Daemon_IO)

```c
/* schemas: "libretro", "libretro+<core>" */
```

`start` callback:
1. Extrai core name e path do ROM da URL.
2. Usa `scanner_resolve_core()` / `scanner_resolve_rom()` para localizar os arquivos
   (lógica atual de `scanner.c`, mantida em `plugin/libretro/`).
3. Carrega core + ROM normalmente (`native_libretro_load` / `native_libretro_game`).

`tick` callback: chama `retro_run()` — sincronizado com o game loop principal.

URLs aceitas:
```
libretro://pepsiman.iso                      ← core resolvido pelo scanner
pcsx_rearmed+libretro://pepsiman.iso         ← core = pcsx_rearmed
libretro+pcsx_rearmed://pepsiman.iso         ← idem (ordem não importa)
```

#### Player 2 — `"libretro+http"` / `"libretro+https"` (buffer via HTTP)

```c
/* schemas: "libretro+http", "libretro+https", "libretro+http+<core>", ... */
```

`start` callback:
1. Extrai core name dos tokens extras (excluindo `libretro`, `http`, `https`).
2. Usa `Daemon_WebClient` para fazer GET da URL e receber o buffer.
3. Quando o buffer chega, chama a lógica interna de carregamento passando
   `retro_game_info.data` + `size` em vez de um path em disco.

`tick`: idem ao player 1, chama `retro_run()`.

Código comum (load/unload de core, callbacks de vídeo/áudio, `retro_run`) fica em
funções internas compartilhadas pelos dois players dentro do mesmo arquivo ou em
`open_libretro_core.c`.

URLs aceitas:
```
http+libretro://tic80.com/hash/foo.tic
http+libretro+tic80://tic80.com/hash/foo.tic
https+libretro+pcsx_rearmed://example.com/pepsiman.iso
```

---

## Decisões finais

- `?` = exatamente um token extra; core sempre explícito — `libretro://rom` sem core é rejeitado
- `plugins/libretro/main.c` contém `luaopen_libretro_gecnd()` + `coreopen_libretro_gecnd()`
  seguindo o padrão de `plugins/chromium/main.c`
- `open_libretro.c` permanece como carregador; `native_libretro_url()` mantida no Lua
- Dois players libretro: `"libretro+?"` (arquivo via Daemon_IO) e `"libretro+http+?"` /
  `"libretro+https+?"` (fetch via Daemon_WebClient + `native_libretro_game_from_buffer`)
- `driver_av_decode.c` recebe os player callbacks no final; sem renomear (CMake intacto)
- CMakeLists.txt: não modificado

## Pendências (implementação)

- [x] Reescrever `service_playback.c` com registry + dispatch + tick
- [x] `driver_av_decode.c`: adicionar player callbacks e `gamely_daemon_media_av_player_register()`
- [x] `open_libretro.c`: tornar `native_libretro_load/game` não-static; adicionar `game_from_buffer`
- [x] Criar `plugins/libretro/main.c` com os dois players e `luaopen/coreopen_libretro_gecnd()`
- [x] Atualizar `gamely_media.h` com `gamely_media_player_t`, `register_player`, `playback_tick`
- [ ] CMakeLists.txt: adicionar `plugins/libretro/main.c` (usuário cuida)
