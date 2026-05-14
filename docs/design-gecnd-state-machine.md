# Design: gecnd — State Machine + Lua Sources

Revisão 7 — 2026-05-13

---

## Objetivo

Formalizar o ciclo de vida do `gecnd_t` como uma **máquina de estados explícita** e
generalizar de onde vem o código Lua (engine e game). Hoje o `update.c` faz tudo
em sequência num único `callback_init()`, lê arquivo local com `fopen()` e
qualquer pedido de `--play` é despachado para o `Daemon_Media` *antes* dos
plugins terem oportunidade de registrar seus schemas de player.

Capacidades novas motivam a refatoração:

1. **`--game URL`** e **`--engine URL`** — baixar scripts via `Daemon_WebClient`
   antes de carregar. Os dois são URLs independentes, podem ser baixados em
   paralelo (estados separados `FETCHING_HTTP_GAME` e `FETCHING_HTTP_ENGINE`).
2. **`--play` sem ambiente pronto** — playback nunca dispara antes do gecnd
   estar em `RUNNING_*` ou `NO_GAME`. Enquanto isso, o pedido fica em uma
   **fila interna do `Daemon_Media`** (não do gecnd). A fila também resolve
   troca de mídia entre players diferentes: o service pede stop, espera o
   player driver **confirmar** o stop, e só então arranca a próxima.
3. **Vendor Lua** — `-DENGINE=...` já existe (gera `engine_bytecode_lua.h` e
   define `GECND_USE_VENDOR_ENGINE`); o mesmo precisa existir para o game
   (`-DGAME=...` → `GECND_USE_VENDOR_GAME`). Já há `#if` em `update.c:15-17`
   esperando esse header.
4. **`NO_GAME`** — `./core --play foo.mp4` sem `--game`: roda só o player de
   mídia, sem engine/script, útil pra testar driver de mídia, filtros e
   transmissão sem precisar de um jogo.
5. **Sub-estados de RUNNING** — formalizar modos de execução já parcialmente
   existentes como flags (`HW_GL_NO_FINSH`, `BROWSER`) ou inexistentes
   (standby): `RUNNING`, `RUNNING_PERFORMANCE` (sem `glFinish`),
   `RUNNING_BACKGROUND` (sem draw, substitui `GECND_INTERNAL_BROWSER`),
   `RUNNING_STANDBY` (sem draw, loop em 1 Hz pra economizar CPU).

---

## Estado atual

### Fluxo linear de inicialização (`update.c:74-148`)

```
gecnd_update()
  └─ se !RUNNING → callback_init()
       1. gly_hook_display_init(w, h)
       2. gecnd_hypervisor_daemons(gly)    [se root]
       3. gly_hook_display_fps(...)
       4. gecnd_plugins_open_lua(L)        ← roda luaopen_* dos plugins
       5. carrega engine:
            - se GECND_USE_VENDOR_ENGINE → luaL_loadbuffer(engine_bytecode_lua)
            - senão → open_script(lua_engine_code, "main", 0)
       6. native_callback_init(w, h, <result of game.lua>)
       7. carrega game:
            - open_script(lua_game_code, "game", 1)
            - chama via lua_pcall(3, 0, 0)
       8. luaL_ref de draw / loop / keyboard
       9. gamely_daemon_input_init_keys(...)
```

Tudo num único bloco `do { ... } while(0)` com `break` em erro. Se qualquer
passo falhar, `gly->error_string` fica setado e `gecnd_update()` retorna `false`
na próxima iteração.

### Carregamento de Lua (`update.c:39-72`, `open_script`)

```c
data.fp = fopen(lua_code, "rb");       /* APENAS arquivo local */
if (!data.fp) return "file not found";
lua_load(L, reader, &data, name, "t");
lua_pcall(L, 0, lua_ret, 0);
```

Sem suporte a HTTP. Não há fallback: se `lua_game_code == NULL`, procura
`{exe_cwd}/game.lua`.

### Flags internas (`gecnd.h:17-23`)

```c
GECND_INTERNAL_MALLOC          1
GECND_INTERNAL_RUNNING         2     ← "já passei pelo callback_init"
GECND_INTERNAL_WANT_EXIT       4
GECND_INTERNAL_BROWSER         8
GECND_INTERNAL_HW_GL_READY    16
GECND_INTERNAL_HW_GL_NO_FINSH 32
```

Não é uma máquina de estados — é um bitset de propriedades. Ortogonais. Não há
representação para "esperando download do game.lua", "engine carregada mas
game ainda não", "rodando sem game".

### `--play` racing com plugins (`set_args.c:81-84`)

```c
if (c == 1307) {                                          /* --play URL */
    gamely_daemon_media_playback_source(0, opt.arg);
    gamely_daemon_media_playback_play(0);
}
```

Disparo é **imediato durante o parse dos argumentos**, antes do
`callback_init()` (e portanto antes de `gecnd_plugins_open_lua()`). Para
schemas que o hypervisor já registrou direto (`""`, `file`, `http`, `https`,
`rtsp`, `rtmp`, `udp`, `ffmpeg+*`, em `hypervisor.c:29-41`) funciona porque
`gecnd_hypervisor()` roda no `main_uvsync.c:18` antes do parse. Mas se o
plugin (libretro, chromium) registra um player próprio em seu `coreopen_*`,
**que só roda em `gecnd_plugins_open_lua`**, então `--play libretro://...`
falha silenciosamente.

### Estado de canal já existe — mas é síncrono e sem confirmação

`service_playback.c:119` define:

```c
typedef enum { CH_IDLE, CH_LOADING, CH_PLAYING, CH_PAUSED } ch_state_t;
```

e em `service_playback.c:175-187` faz a "transição" assim:

```c
ch->state = CH_LOADING;
if (p->cbs.start) p->cbs.start(ch->channel, ch->url, p->usr);
if (ch->state == CH_LOADING) ch->state = CH_PLAYING;   /* assume sucesso */
```

Síncrono. Assume que `start()` deu certo. **O player driver não tem como
informar de volta** "ainda carregando", "falhou", "parei". O contrato
`gamely_media_player_t` em `gecnd.h:390-396` só expõe `start/stop/tick/pause/play`
— nenhum canal de feedback.

Consequência: trocar de player no mesmo canal (ex. `ffmpeg://...` → 
`libretro://...`) pode chamar `start` do novo antes do `stop` do antigo ter
realmente liberado decoder/audio/textures.

### `GECND_USE_VENDOR_GAME` já parcialmente cabeado

`update.c:15-17`:

```c
#if defined(GECND_USE_VENDOR_GAME)
#include "game_bytecode_lua.h"
#endif
```

Mas o `CMakeLists.txt:783-793` só trata `-DENGINE`, não `-DGAME`. O `#if` está
ali mas nunca é usado no fluxo de `callback_init()`.

---

## Proposta

### 1. Máquina de estados explícita

Substituir `GECND_INTERNAL_RUNNING` e `GECND_INTERNAL_BROWSER` por um enum de
estado. `WANT_EXIT`, `HW_GL_READY`, `HW_GL_NO_FINSH` continuam como bitset
porque são propriedades ortogonais (capacidades de backend, sinalizadores
transversais) — **`HW_GL_NO_FINSH` passa a ser dirigido pelo estado**
(`RUNNING_PERFORMANCE` ativa, demais limpam).

```c
typedef enum {
    FSM_GECND_BOOT,                /* alocado, sem args parseados */
    FSM_GECND_ARGS_PARSED,         /* set_args terminou, sem daemons */
    FSM_GECND_DAEMONS_UP,          /* hypervisor_daemons + plugins_open_lua */
    FSM_GECND_FETCHING_HTTP_ENGINE,/* baixando main.lua via HTTP */
    FSM_GECND_FETCHING_HTTP_GAME,  /* baixando game.lua via HTTP */
    FSM_GECND_ENGINE_LOADED,       /* main.lua executado */
    FSM_GECND_GAME_LOADED,         /* game.lua + native_callback_init ok */
    FSM_GECND_RUNNING,             /* loop normal: input → loop → draw */
    FSM_GECND_RUNNING_PERFORMANCE, /* idem, sem glFinish (latência > consistência) */
    FSM_GECND_RUNNING_BACKGROUND,  /* sem draw (substitui GECND_INTERNAL_BROWSER) */
    FSM_GECND_RUNNING_STANDBY,     /* sem draw + loop em 1 Hz, economia CPU */
    FSM_GECND_RUNNING_NOGAME,      /* --play sem --game, sem engine */
    FSM_GECND_ERROR,               /* error_string preenchida */
    FSM_GECND_EXITING,             /* WANT_EXIT visto, aguardando mídia parar */
    FSM_GECND_EXITING_FORCE,       /* segundo SIGINT durante EXITING, sem esperar */
} fsm_gecnd_state_t;
```

`FETCHING_HTTP_ENGINE` e `FETCHING_HTTP_GAME` ficam como **estados
sequenciais separados** (não paralelos). O `Daemon_WebClient` é assíncrono
em si, mas para o gecnd serializar é mais simples: termina engine, depois
game. Evita lógica de "fetch é a junção de dois futures". Pode virar
paralelo depois se virar gargalo na prática.

`gecnd_update()` despacha por estado. Cada estado tem **uma** responsabilidade
clara e uma transição de saída:

```
BOOT ──set_args──► ARGS_PARSED ──hypervisor──► DAEMONS_UP
                                                │
                       ┌────────────────────────┼─── (sem --game E com --play) ──► RUNNING_NOGAME
                       │                        │
                       │ (engine é URL?)        │ (engine local/vendor)
                       ▼                        ▼
              FETCHING_HTTP_ENGINE ──ok──► ENGINE_LOADED
                       │                        │
                       │                        │ (game é URL?)        │ (game local/vendor)
                       │                        ▼                      ▼
                       │              FETCHING_HTTP_GAME ──ok──► GAME_LOADED ──► RUNNING
                       │
                       └─error─► ERROR

  RUNNING ⇄ RUNNING_PERFORMANCE ⇄ RUNNING_BACKGROUND ⇄ RUNNING_STANDBY    (transições em runtime)
  RUNNING_NOGAME (independente, sem transições pra outros RUNNING_*)

  qualquer estado ──error/signal/WANT_EXIT──► EXITING
```

Comportamento por sub-estado:

| Estado | input tick | callback_loop | callback_draw | glFinish | tick rate |
|---|---|---|---|---|---|
| `RUNNING`              | ✓ | ✓ | ✓ | ✓ | target_fps |
| `RUNNING_PERFORMANCE`  | ✓ | ✓ | ✓ | — | target_fps |
| `RUNNING_BACKGROUND`   | ✓ | ✓ | — | — | target_fps |
| `RUNNING_STANDBY`      | ✓ | ✓ | — | — | 1 Hz |
| `RUNNING_NOGAME`       | ✓ | — | frame de mídia + filtros + metrics | ✓ | target_fps |

Transição entre os sub-RUNNING é **disparada externamente** (CLI ou caller
do binário), via API:

```c
void gecnd_set_state(gecnd_t *gly, fsm_gecnd_state_t new_state);
```

com validação: só permite transições dentro do grupo `RUNNING_*`. Não
permite voltar pra `BOOT`/`FETCHING_*` etc. `RUNNING_NOGAME` é exclusivo
(não transita pros outros `RUNNING_*` — quem entra aí não tem engine
carregada, então não tem como virar `RUNNING` "normal").

### 2. Lua source plugável + cadeia de fallback

Abstrair `open_script()` em um resolver com **cadeia de fallback**. Para
cada slot (engine, game) o resolver tenta candidatos em ordem; se um falha
("não disponível" ou "erro de fetch/parse"), cai pro próximo:

```
1. --engine/--game (HTTP ou FILE — classificado por prefixo http://, https://)
2. Vendor bytecode (se compilado com -DENGINE / -DGAME)
3. Disk fallback ({exe_cwd}/main.lua, {exe_cwd}/game.lua — comportamento atual)
```

Se nenhum dos três funciona → `FSM_GECND_ERROR` com mensagem listando os
candidatos tentados.

```c
typedef enum {
    GECND_LUA_SOURCE_FILE,        /* path local */
    GECND_LUA_SOURCE_HTTP,        /* baixar via Daemon_WebClient */
    GECND_LUA_SOURCE_VENDOR,      /* bytecode embedded no binário */
    GECND_LUA_SOURCE_FALLBACK_FS, /* {exe_cwd}/{name}.lua */
} gecnd_lua_source_kind_t;

typedef struct {
    gecnd_lua_source_kind_t kind;
    const char             *uri;          /* path ou URL; NULL pra VENDOR/FALLBACK_FS */
    const uint8_t          *embedded;     /* só pra VENDOR */
    size_t                  embedded_len;
} gecnd_lua_source_t;
```

`set_args.c` classifica a string de `--game`/`--engine`:

```c
if (c == 1305) {
    if (strncmp(opt.arg, "http://",  7) == 0 ||
        strncmp(opt.arg, "https://", 8) == 0) {
        gly->game_source = (gecnd_lua_source_t){ GECND_LUA_SOURCE_HTTP, opt.arg };
    } else {
        gly->game_source = (gecnd_lua_source_t){ GECND_LUA_SOURCE_FILE, opt.arg };
    }
}
```

A combinação `-DENGINE` + `--engine URL` é válida e útil: roda com a URL,
mas se ela falhar (offline, URL morta), cai pro bytecode embedded. Os dois
`-D*` são **independentes** (`-DENGINE` sem `-DGAME` ou vice-versa é caso
suportado de primeira classe — engine vendorizado fixo + game externo é
caso comum).

### 3. CMake `-DGAME` simétrico ao `-DENGINE`

Adicionar bloco gêmeo em `CMakeLists.txt:783-793`:

```cmake
if(DEFINED GAME)
    execute_process(
        WORKING_DIRECTORY ${GLY_DIR}
        COMMAND ${LUA_BIN} ${GLY_CLI} fs-xxd-i ${GAME} --const --name game_bytecode_lua
            ${CMAKE_BINARY_DIR}/include/game_bytecode_lua.h
        COMMAND_ERROR_IS_FATAL ANY
    )
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_Core/update.c"
        PROPERTIES COMPILE_DEFINITIONS "GECND_USE_VENDOR_GAME=1"
    )
endif()
```

E o `update.c:114` passa a respeitar `GECND_USE_VENDOR_GAME`:

```c
#if defined(GECND_USE_VENDOR_GAME)
    luaL_loadbuffer(L, game_bytecode_lua, game_bytecode_lua_len, "G");
    if (lua_pcall(L, 0, 1, 0)) { ... }
#else
    error = load_via_resolver(gly, &gly->game_source, 1);
#endif
```

Cuidado: se `-DENGINE` e `-DGAME` estão definidos *e* `-DGAME` está
definido sem o `-D` simétrico, é erro de build — falhar cedo no CMake.

### 4. Estado `NO_GAME`

Se ao chegar em `DAEMONS_UP` temos:
- `game_source.kind == FILE && uri == NULL` (não passou `--game`)
- `!GECND_USE_VENDOR_GAME`
- mas o daemon de mídia tem playback ativo (`gamely_daemon_media_playback_active()`)

então vai direto pra `NO_GAME`. Nesse modo:

- não tenta carregar engine
- não chama `native_callback_*`
- `gecnd_update()` tica `daemon_media_playback_tick`, `daemon_input_tick`,
  `daemon_fs_tick`, e desenha o frame de mídia (background_get_frame) com
  filtros aplicados
- sai quando o playback termina ou sinal de exit

Útil pra testar: drivers de mídia, filtros visuais, transmissão (encoder).

### 5. Fila e confirmação de stop dentro do `Daemon_Media`

A fila de `--play` **não pertence ao gecnd**. Pertence ao próprio
`Daemon_Media`. O gecnd só fornece um **gate**: o playback service consulta
o estado do gecnd e só drena a fila quando está em `FSM_GECND_RUNNING_*`
(incluindo `RUNNING_NOGAME`). Antes disso (FETCHING_*, DAEMONS_UP, etc),
pedidos ficam parados.

Isso também resolve a troca de mídia entre players diferentes (caso comum:
`ffmpeg://video.mp4` → `libretro://rom.bin`). O service hoje em
`service_playback.c:172-187` faz stop + start sequencialmente assumindo que
`stop()` é síncrono e ja liberou os recursos. **Não é** — drivers como
FFmpeg ou libretro precisam de tempo pra fechar decoder, audio backend,
texturas. Precisa de confirmação.

#### Mudança 1: `gamely_media_player_t` ganha estado consultável

Estender o contrato em `gecnd.h:390-396`:

```c
typedef enum {
    FSM_GDMSP_IDLE,        /* sem fonte */
    FSM_GDMSP_LOADING,     /* start() em andamento, ainda não tocando */
    FSM_GDMSP_PLAYING,     /* tocando */
    FSM_GDMSP_PAUSED,
    FSM_GDMSP_STOPPING,    /* stop() em andamento, ainda não liberado */
    FSM_GDMSP_ERROR,
} fsm_gdmsp_state_t;

typedef struct {
    void (*start)(uint8_t channel, const char *url, void *usr);
    void (*stop) (uint8_t channel, void *usr);
    void (*tick) (uint8_t channel, void *usr);
    void (*pause)(uint8_t channel, void *usr);
    void (*play) (uint8_t channel, void *usr);
    /* OBRIGATÓRIO. Lock-free: deve retornar imediatamente lendo um snapshot
     * atômico interno do driver (uma única variável que o driver atualiza
     * conforme avança no ciclo de vida). Nada de mutex, nada de I/O. */
    fsm_gdmsp_state_t (*state)(uint8_t channel, void *usr);
} gamely_media_player_t;
```

`state()` é **obrigatório** e **lock-free**. O contrato: o driver mantém
internamente uma variável atômica (`atomic_int` ou equivalente) e
`state()` apenas faz um load dela. Chamada a cada tick do playback service,
não pode bloquear.

Sem fallback síncrono: o service sempre confia no que o driver reporta.
Isso elimina a "transição automática" otimista da `service_playback.c:177,187`
e força os drivers a expor honestamente seu ciclo de vida.

#### Mudança 2: canal usa `fsm_gdmsp_state_t` diretamente + operação pendente

Como `state()` é obrigatório e confiável, o `ch_state_t` antigo de
`service_playback.c:119` deixa de ser uma duplicação. O canal guarda
o estado **do driver** mais um **slot de operação pendente**:

```c
typedef struct {
    fsm_gdmsp_state_t  driver_state;   /* última leitura de player->cbs.state() */
    char              *url;
    player_entry_t    *player;
    /* operação enfileirada — usada quando precisa swap entre players
     * diferentes (precisa esperar STOPPING → IDLE primeiro), ou
     * quando o gate global do gecnd ainda não permite play.
     * pending_url == NULL → sem nada pendente.
     * pending_play: true = autoplay após start, false = começa pausado. */
    char              *pending_url;
    bool               pending_play;
} channel_t;
```

A cada `gamely_daemon_media_playback_tick()`:

1. Para cada canal: `driver_state = player->cbs.state(channel, usr)`
   (snapshot atômico, lock-free).
2. **`FSM_GDMSP_ERROR` é tratado como gatilho de stop**: o service chama
   `stop()` no driver e passa a esperar `STOPPING → IDLE` como em qualquer
   stop normal. ERROR não é terminal — vira IDLE no próximo ciclo após o
   stop completar.
3. Se gate global do gecnd estiver em `FSM_GECND_EXITING`: para cada canal
   ainda não IDLE, chama `stop()` (uma vez, idempotente) e aguarda
   `FSM_GDMSP_IDLE` antes de permitir que o `gecnd_update()` retorne `false`.
4. Se gate global fora de `FSM_GECND_RUNNING_*` (e não EXITING) → return;
   nada drena.
5. Drain de operação pendente (gate aberto):
   - Há `pending_url` e canal está vazio (sem player corrente, ou
     `driver_state == FSM_GDMSP_IDLE`) → resolve schema, anexa player,
     chama `start()`. Driver passa a reportar `LOADING` → `PLAYING`.
   - Há `pending_url` e canal está ocupado (`PLAYING`/`PAUSED`/`LOADING`/`ERROR`)
     → chama `stop()` no driver atual; espera próximo tick.
   - Sem pending → nada a fazer.
6. Pedido novo `gamely_daemon_media_playback_source(url)`:
   - Grava em `pending_url` do canal.
   - O drain do passo 5 resolve no(s) próximo(s) tick(s).

**Regra de troca de player**: outra mídia de outro player só toca quando
o canal está realmente vazio (sem player atual, ou player atual em
`FSM_GDMSP_IDLE`). Nunca pula etapas — é sempre `stop → STOPPING → IDLE → start`.

#### Mudança 3: `set_args.c` chama API como sempre

```c
if (c == 1307) {                                          /* --play URL */
    gamely_daemon_media_playback_source(0, opt.arg);
    gamely_daemon_media_playback_play(0);
}
```

Sem mudança aqui. **A inteligência é toda do `service_playback.c`**. O
service vê o gate fechado, marca o canal como "pending", e drena quando o
gecnd notificar (ou quando o service consultar o gate no próximo tick).
Isso também é o sinal pro gecnd transicionar pra `NO_GAME`: se em
`DAEMONS_UP` `gamely_daemon_media_playback_active()` retorna true E não há
`--game`/vendor game, entra em `NO_GAME` em vez de tentar carregar engine.

> Nota: se `--play` referencia um schema que **nenhum** player registrou
> (nem hypervisor nem plugin), agora o erro é detectável: ao drenar a fila
> com gate aberto, o resolve falha → `error_string` no canal e log. Hoje
> isso desaparece silenciosamente.

---

## Mudanças de API (`gecnd.h`)

Adicionar:

```c
/* state */
gecnd_state_t gecnd_get_state(gecnd_t *gly);
const char   *gecnd_state_name(gecnd_state_t s);   /* pra log/debug */

/* lua source — pra quem quer setar programaticamente sem CLI */
void gecnd_set_engine_source(gecnd_t *gly, const gecnd_lua_source_t *src);
void gecnd_set_game_source  (gecnd_t *gly, const gecnd_lua_source_t *src);
```

Deprecar (mas manter por compat enquanto a transição acontece):

```c
char *lua_game_code;       /* → game_source.uri quando kind==FILE */
char *lua_engine_code;     /* idem */
```

---

## Impacto por arquivo

| Arquivo | Mudança |
|---|---|
| `include/gecnd.h` | enums `fsm_gecnd_state_t`, `fsm_gdmsp_state_t`, `gecnd_lua_source_kind_t`; struct `gecnd_lua_source_t`; adicionar `state` (obrigatório) a `gamely_media_player_t`; getter `gecnd_get_state` / setter `gecnd_set_state` (com validação de transição); manter `lua_game_code/lua_engine_code` como deprecated |
| `lib/Frontend_Core/instance.c` | inicializar `state = BOOT` |
| `lib/Frontend_Core/set_args.c` | classificar `--game`/`--engine` (file vs http vs vendor); `--play` continua chamando a mesma API do Daemon_Media (fila é interna lá) |
| `lib/Frontend_Core/update.c` | **rewrite do `callback_init` como state dispatcher**; `open_script` → `load_via_resolver` (file/http/vendor); branches por `state` em `gecnd_update`, incluindo loop 1 Hz em `RUNNING_STANDBY` e skip de draw em `RUNNING_BACKGROUND` |
| `lib/Frontend_Core/hypervisor.c` | sem mudança funcional |
| `lib/Backend_OpenGL/render/draw.c` | já honra `HW_GL_NO_FINSH` (linhas 12-14); o estado `RUNNING_PERFORMANCE` agora seta/limpa essa flag (não muda o backend) |
| `lib/Daemon_Media/service_playback.c` | **gate global**: ler `gecnd_get_state` no tick e bloquear drain fora de `FSM_GECND_RUNNING_*`; substituir `ch_state_t` interno por leitura de `player->cbs.state()` + slot `pending_url`/`pending_autoplay` por canal |
| `lib/Daemon_Media/driver_av_*.c` | implementar `state()` (obrigatório) |
| `plugins/libretro/*`, `plugins/chromium/*` | implementar `state()` no `gamely_media_player_t` exportado (obrigatório; load_game do libretro mapeia naturalmente pra LOADING→PLAYING) |
| `CMakeLists.txt` | bloco `if(DEFINED GAME)` simétrico ao de `ENGINE` |
| `src/main_*.c` | sem mudança (a transição é interna ao `gecnd_update`) |

---

## Revisão 7 — Limpeza do `gecnd_t` e `gecnd_lua_source_t`

### Motivação

Ao implementar os estados de fetch HTTP, ficou evidente que `gecnd_t` acumula
campos que não pertencem ao objeto de jogo: config do hypervisor, campos mortos
e flags de hardware que deveriam ser dirigidas pelo FSM. Esta revisão formaliza
o corte.

---

### Campos removidos de `gecnd_t`

| Campo | Motivo | Destino |
|---|---|---|
| `browser_bin` | Nunca usado em nenhum `.c` fora do header | Removido |
| `ref_native_callback_init` | `native_callback_init` é chamado uma vez; ref nunca lida | Removido |
| `scale_factor` | Só inicializado (`1.0f`), nunca lido em nenhum `.c` | Removido |
| `port` | Config do webserver — pertence ao hypervisor | `gecnd_display_t` |
| `window_width` / `window_height` | Dimensões do display — não são resolução do jogo | `gecnd_display_t` |
| `disable_radius` | Config de render global, não é estado do jogo | `gecnd_display_t` |
| `want_blit` | **Fica em `gecnd_t`** — é estado por instância (set antes de `callback_draw`, limpo pela Lua via `native_blit`) | — |

---

### Flags internas eliminadas (viram estado FSM)

| Flag (bitset antigo) | Substituto |
|---|---|
| `GECND_INTERNAL_MALLOC (1)` | Removido — morto desde a migração para malloc puro |
| `GECND_INTERNAL_WANT_EXIT (4)` | `GECND_FSM_EXITING` — `system.c` seta `gly->state` diretamente |
| `GECND_INTERNAL_BROWSER (8)` | `GECND_FSM_RUNNING_BACKGROUND` — chromium plugin chama `gecnd_set_state()` |
| `GECND_INTERNAL_HW_GL_NO_FINSH (32)` | `GECND_FSM_RUNNING_PERFORMANCE` — libretro chama `gecnd_set_state()` ao carregar qualquer core (não só HW render — libretro sempre dispensa `glFinish`); `draw.c` lê o estado |

**Flag que permanece** como bitset (capacidade de hardware, ortogonal ao FSM):

| Flag | Motivo |
|---|---|
| `GECND_INTERNAL_HW_GL_READY (16)` | Sinalizado pelo backend (egl/glfw) quando o contexto GL está pronto; lido por libretro antes de habilitar HW rendering. Não é estado do jogo — é capacidade do backend. |

---

### `gecnd_lua_source_t` — union `embedded` / `fetch`

`embedded` (VENDOR) e `fetch` (HTTP) são mutuamente exclusivos pelo `kind`:
uma fonte ou veio compilada no binário ou precisa ser baixada, nunca os dois.
Usar uma union economiza memória e torna o contrato óbvio no código.

```c
typedef struct {
    gecnd_lua_source_kind_t  kind;
    const char              *uri;    /* path local ou URL; NULL pra VENDOR */
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
        } fetch;                     /* kind == GECND_LUA_SOURCE_HTTP */
    };
} gecnd_lua_source_t;
```

Os callbacks HTTP recebem `gecnd_lua_source_t *src` como `user` e escrevem
direto em `src->fetch.*`. O `gecnd_update()` lê de `gly->engine_source` ou
`gly->game_source` conforme o estado atual.

---

### `gecnd_display_t` — struct global de display/hypervisor

Campos que são globais ao processo (display, webserver, render flags) saem
do `gecnd_t` e ficam em uma struct singleton acessível via `gecnd_get_display()`.

```c
typedef struct {
    int16_t  window_width;
    int16_t  window_height;
    uint16_t port;
    bool     want_blit;
    bool     disable_radius;
} gecnd_display_t;

gecnd_display_t *gecnd_get_display(void);
```

Implementada como `static gecnd_display_t g_display` em `hypervisor.c`.
`gecnd_get_display()` retorna `&g_display` — singleton do processo.

**Ciclo de inicialização:**
1. `set_args.c` escreve em `gecnd_get_display()->port`, `->window_width/height`,
   `->disable_radius` conforme os args CLI.
2. `state_boot()` chama `gecnd_hypervisor_daemons(gly)` (só no root).
3. Dentro de `gecnd_hypervisor_daemons()`, se `g_display.window_width == 0`,
   copia `gly->width/height` como fallback (comportamento atual preservado).
4. A partir daí, `set_filter.c` e `draw.c` lêem de `gecnd_get_display()`.

---

### `gecnd_set_state()` — API pública para transições externas

```c
/* Permite transições dentro do grupo RUNNING_*
 * (RUNNING ↔ RUNNING_PERFORMANCE ↔ RUNNING_BACKGROUND ↔ RUNNING_STANDBY).
 * RUNNING_NOGAME é exclusivo. Transições para ERROR/EXITING não passam aqui. */
void gecnd_set_state(gecnd_t *gly, gecnd_fsm_t new_state);
```

Implementação em `instance.c`. Usada por:
- **chromium plugin**: `gecnd_set_state(gly, GECND_FSM_RUNNING_BACKGROUND)` ao lançar o browser; `gecnd_set_state(gly, GECND_FSM_RUNNING)` ao fechar.
- **libretro plugin**: `gecnd_set_state(gly, GECND_FSM_RUNNING_PERFORMANCE)` quando `RETRO_HW_RENDER`; `gecnd_set_state(gly, GECND_FSM_RUNNING)` ao descarregar.

`draw.c` lê `gly->state` diretamente para decidir se chama `glFinish` e se
suprime o draw.

---

### `gecnd_t` — struct final após limpeza

```c
typedef struct {
    lua_State          *L;
    void               *loop;
    uint8_t             target_fps;
    uint8_t             frameskip;
    uint8_t             frameskip_count;
    uint8_t             flags;
    uint8_t             internal;   /* apenas HW_GL_READY */
    gecnd_fsm_t         state;
    int16_t             width;      /* resolução do jogo (render target) */
    int16_t             height;
    int16_t             delta_time;
    int                 ref_native_callback_loop;
    int                 ref_native_callback_draw;
    int                 ref_native_callback_keyboard;
    gecnd_lua_source_t  game_source;
    gecnd_lua_source_t  engine_source;
    const char         *error_string;
} gecnd_t;
```

`window_width/height`, `port`, `want_blit`, `disable_radius` → `gecnd_display_t`.
`scale_factor` → removido (dead field).
`internal` fica só com `GECND_INTERNAL_HW_GL_READY` — o `HW_GL_NO_FINSH`
virou `GECND_FSM_RUNNING_PERFORMANCE`.

---

### Impacto adicional por arquivo (Rev 7)

| Arquivo | Mudança |
|---|---|
| `include/gecnd.h` | Remove macros mortos (`INTERNAL_MALLOC/WANT_EXIT/BROWSER/HW_GL_NO_FINSH`); union `embedded`/`fetch` em `gecnd_lua_source_t`; remove campos de `gecnd_t`; adiciona `gecnd_display_t` + `gecnd_get_display()`; declara `gecnd_set_state` |
| `lib/Frontend_Core/instance.c` | `gecnd_destroy` libera `engine_source.fetch.buf` e `game_source.fetch.buf`; implementa `gecnd_set_state` com validação RUNNING_* |
| `lib/Frontend_Core/set_args.c` | `gly->port/window_width/window_height/disable_radius` → `gecnd_get_display()->...` |
| `lib/Frontend_Core/hypervisor.c` | `static gecnd_display_t g_display`; implementa `gecnd_get_display()`; usa `g_display.port` em `gamely_daemon_webserver_start` |
| `lib/Frontend_Core/set_filter.c` | `gly->window_width/height` → `gecnd_get_display()->window_width/height` |
| `lib/Frontend_Core/update.c` | Rewrite: dispatcher de estados; handlers `state_boot … state_game_loaded`; remove `callback_init` monolítico; `want_blit` permanece em `gly` |
| `lib/Frontend_Api/system.c` | `gly->internal \|= GECND_INTERNAL_WANT_EXIT` → `gly->state = GECND_FSM_EXITING` |
| `lib/Frontend_Api/draw.c` | `gly->disable_radius` → `gecnd_get_display()->disable_radius`; `want_blit` permanece em `gly` |
| `lib/Backend_OpenGL/render/draw.c` | `GECND_INTERNAL_BROWSER` → `state == GECND_FSM_RUNNING_BACKGROUND`; `GECND_INTERNAL_HW_GL_NO_FINSH` → `state == GECND_FSM_RUNNING_PERFORMANCE` |
| `plugins/chromium/main.c` | `GECND_INTERNAL_BROWSER` set/clear → `gecnd_set_state(gly, GECND_FSM_RUNNING_BACKGROUND/RUNNING)` |
| `plugins/libretro/open_libretro.c` | `GECND_INTERNAL_HW_GL_NO_FINSH` set/clear → `gecnd_set_state(gly, GECND_FSM_RUNNING_PERFORMANCE/RUNNING)` — ocorre no `coreopen`/`coreclose`, não no RETRO_HW_RENDER |

---

## Decisões fechadas

### Estados e naming
- Fetching de engine e game ficam em **estados separados sequenciais**.
- `FSM_GECND_*` pra estados do gecnd; `FSM_GDMSP_*` pra estados do Gamely
  Daemon Media Service Playback.
- Função do contrato chama-se `state()` (não `get_state`).
- Transições entre `RUNNING_*` são disparadas **externamente** (não via Lua
  no momento).
- `RUNNING_NOGAME` desenha frame de mídia + filtros + metrics.

### Lua loading
- **Sem cache** de game/engine.lua baixado.
- **Sem retry** de fetch. Falha → `FSM_GECND_ERROR`.
- **Sem schema `lua://inline`** por enquanto.

### Player driver contract
- `state()` é **obrigatório** e **lock-free**. Driver mantém variável
  atômica interna; `state()` apenas faz load. Drivers existentes
  (`driver_av_*`, libretro, chromium) serão atualizados como parte desta
  refatoração.

### Service de playback
- `FSM_GECND_EXITING` aguarda mídia parar: chama `stop()` em todos os canais
  e espera todos virarem `FSM_GDMSP_IDLE` antes de `gecnd_destroy`.
- `FSM_GECND_EXITING_FORCE` (segundo SIGINT durante EXITING): não espera
  mídia, libera o caminho de destroy imediatamente. Driver pode vazar
  recursos — aceito porque é abort.
- **Sem timeout automático** no EXITING. Se um driver bugar e nunca virar
  IDLE, o app fica esperando até o user mandar segundo Ctrl+C
  (que promove pra EXITING_FORCE). Mantém o controle com quem ativou o exit.
- `FSM_GDMSP_ERROR` **não é terminal** — service trata como gatilho de
  `stop()` e canal volta pra IDLE no ciclo seguinte.
- Troca de player só acontece quando o canal está vazio (sem player atual,
  ou player atual em `FSM_GDMSP_IDLE`). Sempre `stop → STOPPING → IDLE → start`,
  nunca pula etapas.
- Resolução de schema do player acontece **no drain** (não no `source()`),
  pra capturar plugins registrados depois.

### Lua source
- Cadeia de fallback: `--flag` → vendor (se `-DENGINE`/`-DGAME`) → `{exe_cwd}/{name}.lua`.
- `-DENGINE` e `-DGAME` são **independentes** — qualquer combinação é
  suportada (engine vendor + game externo, ou inverso).
- **Semântica de falha na cadeia**:
  - *Fonte indisponível* (HTTP 4xx/5xx/timeout, FILE not found, VENDOR
    não compilado) → tenta próximo elo.
  - *Fonte disponível mas conteúdo inválido* (erro de `lua_load` ou
    `lua_pcall`) → `FSM_GECND_ERROR` direto. Não mascara bugs de sintaxe
    no script do user com fallback silencioso pro vendor.

---

**Design fechado.** Próximo passo: implementação incremental. Sugestão de
ordem (cada item compilando e testável isoladamente):

1. Renomear flag `GECND_INTERNAL_RUNNING` → `fsm_gecnd_state_t` (só
   `BOOT`/`RUNNING`/`EXITING`/`ERROR` primeiro, sem mudar fluxo).
2. Adicionar `state()` obrigatório ao `gamely_media_player_t` e implementar
   em todos os drivers existentes (cada um expõe atomic interno).
3. Refatorar `service_playback.c` pra usar `state()` + `pending_url`,
   remover `ch_state_t` antigo.
4. Adicionar gate de `FSM_GECND_RUNNING_*` no service.
5. Bloco CMake `-DGAME` simétrico ao `-DENGINE`; usar `GECND_USE_VENDOR_GAME`
   no `update.c`.
6. Adicionar `gecnd_lua_source_t` + resolver (file primeiro, http depois).
7. Adicionar `FETCHING_HTTP_ENGINE`/`FETCHING_HTTP_GAME` ao FSM.
8. Adicionar `RUNNING_NOGAME` (entrada quando `--play` sem `--game`).
9. Sub-estados `RUNNING_PERFORMANCE`/`BACKGROUND`/`STANDBY` (renomeia/elimina
   `GECND_INTERNAL_BROWSER`).
