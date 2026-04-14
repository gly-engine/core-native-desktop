# Plano: Daemon_Inputs

## Estado atual relevante

| Arquivo | O que importa |
|---|---|
| `key_map.c` | Keymaps NEC hardcoded + `gecnd_key_from_name` |
| `key_state.c` | `bool key_states[GECND_KEY_COUNT]` — array fixo, só port 0 → **migra para `service_io.c`** (dinâmico) |
| `key_queue.c` | Fila SPSC com mutex; `gecnd_set_btn_state` enfileira, `gecnd_input_poll_events` desfila → **migra para `service_io.c`** |
| `open_aui.c` | Thread AUI → `gecnd_set_btn_state` → fila → main loop → **migra para `driver_aui.c`** |
| `update.c:241` | `gecnd_input_poll_events` chamado no main loop |

O padrão thread→fila→main loop já existe mas está acoplado ao código legado.  
Os novos drivers seguirão o mesmo padrão de forma organizada via daemon.

---

## Convenção de prefixos

| Prefixo | Escopo |
|---|---|
| `gecnd_` | API pública — `gecnd.h`, usada por frontends, Lua, libretro |
| `gamely_` | Uso interno — daemons, drivers, helpers entre módulos |

---

## Flags CLI

### `--toml <arquivo>`

Único ponto de entrada do TOML. Sem carregamento automático.

Em `set_args.c` chama `gamely_set_toml(gly, opt.arg)` (implementado em `set_toml.c`).  
`gamely_set_toml` aplica `[args]` do TOML exatamente como se fossem args CLI passados naquela posição — sem lógica especial de merge, sem chamadas circulares (`--toml` dentro do TOML é ignorado).

Prioridade de sobreescrita em ordem de ocorrência:
```
args anteriores ao --toml  <  [args] do TOML  <  args posteriores ao --toml
```

`set_toml.c` não mantém estado estático. Após parsear o TOML:
1. Traversa recursivamente `[keymap]` — para cada tabela folha (valores são arrays, não sub-tabelas), reconstrói o caminho dotted completo e chama `gamely_daemon_input_add_class(path)` seguido de `gamely_daemon_input_add_keycode` para cada entrada
2. Aplica `[args]` ao `gly` como se fossem args CLI (incluindo `gly->input`)
3. Chama `toml_free` e retorna — nenhum dado TOML persiste

### `--input <uri>`

Define `gly->input = opt.arg`. O último valor vence. `gamely_daemon_input_open` é chamado posteriormente, na inicialização das daemons, não aqui.

---

## Formato URI de input

```
<protocolo>://<keymap>?device=<path>&debug=<0|1>
```

| Protocolo | Driver | Fonte | `device` |
|---|---|---|---|
| `void` | `driver_void.c` | sem dispositivo | ignorado |
| `lirc` | `driver_lirc.c` | `/dev/lircN` | obrigatório |
| `serial` | `driver_serial.c` | porta serial | obrigatório |
| `read` | `driver_read.c` | device file genérico | obrigatório |
| `aui` | `driver_aui.c` | `libaui.so` via dlopen | ignorado |

O `<keymap>` é o nome da classe registrada via `gamely_daemon_input_add_class`.  
`void://vivensis.dtv3` → `service_keymap.c` localiza a classe `"vivensis.dtv3"` no registry.  
Keymap `0` = sem classe; driver abre com `km = NULL`.

---

## TOML

```toml
[args]
input = "void://ali.century"

[keymap.ali.century]
a      = [0xF1, 0xF2]    # nome de botão: máx 8 bytes, erro se exceder
up     = [0xF3]

[keymap.vivensis.dtv3]    # "vivensis.dtv3" é a chave única registrada via add_class
a = [0xF1, 0xF2]
```

Todos os keymaps hardcoded de `key_map.c` migram para `config.toml`. Nomes sem espaços:

| Antigo | Novo `[keymap.*]` |
|---|---|
| `"century"` | `[keymap.century]` |
| `"intelbras"` | `[keymap.intelbras]` |
| `"montage"` | `[keymap.montage]` |
| `"vivensis dtv 3.0"` | `[keymap.vivensis.dtv30]` |
| `"vivensis vx smart"` | `[keymap.vivensis.vxsmart]` |

Nomes de botão têm **7 chars úteis + `'\0'`** (8 bytes totais). `service_keymap.c` valida em tempo de carregamento e loga erro ignorando a entrada se ultrapassar.

---

## Modo debug (`debug=1`)

Uma linha por evento recebido no driver:

```
[core:debug:input] hex=%08X class=%s key=%s press=%d
```

`hex` sempre impresso. `class` = keymap ativo ou `none`. `key` = botão resolvido ou `?`.

Sem `debug=1`: após `gamely_daemon_input_open`, todas as `gamely_keymap_t` não utilizadas são descartadas.  
Com `debug=1`: todas as classes registradas são mantidas para escanear no print de diagnóstico.

---

## `service_io.c` — estado, fila e TTL (dinâmicos)

Não existe `GECND_KEY_COUNT` fixo. Tudo alocado de acordo com os inputs recebidos. API interna, não exposta em `gecnd.h`.

```c
// interno — service_io.c apenas
void gamely_key_set(int port, const char *name, bool pressed);
bool gamely_key_get(int port, const char *name);
void gamely_key_reset_port(int port);
```

### Bucket de nomes (`gamely_keyname_bucket_t`)

Pool de strings interning compartilhado por todas as estruturas internas. Evita duplicar `char name[8]` em cada entrada — estado, fila e heap usam apenas `const char *` apontando para dentro do bucket.

```c
typedef struct {
    char   (*pool)[8]; // array dinâmico de slots fixos de 8 bytes
    int    count;
    int    capacity;
} gamely_keyname_bucket_t;

// retorna ponteiro estável para o nome já existente ou insere novo
const char *gamely_keyname_intern(gamely_keyname_bucket_t *b, const char *name);
```

**Ownership**: bucket pertence a `service_keymap.c`. `service_io.c` recebe `const char *name` já resolvido pelo keymap e armazena apenas o ponteiro.

**Thread safety**: o bucket é escrito exclusivamente durante a fase de montagem (`add_keycode`), antes de qualquer driver ser aberto. Após `gamely_daemon_input_open()` o bucket é somente-leitura — ponteiros são estáveis e múltiplas threads podem lê-los sem lock.  
`intern()` não precisa de mutex — só chamado na thread principal antes do `open`.

Consequência: `gamely_keymap_entry_t.name` vira `const char *name` (ponteiro para o bucket), assim como as entradas de estado, fila e heap.

```c
typedef struct { uint32_t code; const char *name; } gamely_keymap_entry_t;
```

**Estado**: array dinâmico de `{const char *name; bool pressed[4];}` — busca linear por ponteiro (comparação de endereço, O(n) mas conjunto pequeno).

**Fila de eventos**: SPSC com mutex, migrada de `key_queue.c`. Evento: `{const char *name; bool pressed; int port;}`. Enfileirado por `push` (threads dos drivers), drenado por `gamely_daemon_input_tick` na thread principal.

**TTL**: array flat de `{const char *name; int port; uint64_t expiry_ms;}` — apenas entradas com TTL pendente existem. Se `push` chega com o mesmo `(name, port)` antes da expiração, atualiza `expiry_ms` no lugar. `gamely_daemon_input_tick` faz scan linear e despacha expirados. O(n) trivial — n é limitado ao número de teclas da classe ativa (conjunto pequeno).

`key_state.c` e `key_queue.c` são deletados.

---

## `key_queue.c` — port no evento

```c
typedef struct {
    char key[32];
    bool pressed;
    int  port;
} gecnd_key_event_t;
```

`gecnd_set_btn_state` continua enfileirando port 0 (usado pelos drivers em thread e backends como GLFW).  
Drivers do `Daemon_Inputs` rodam em threads separadas e enfileiram via `gecnd_set_btn_state` — IR é sempre port 0.

---

## `update.c` — `gecnd_dispatch_key_event`

Nova assinatura:

```c
void gecnd_dispatch_key_event(gecnd_t *gly, const char *name, bool pressed, int port);
```

Port 0: `gamely_key_set(0, name, pressed)` + Lua callback por nome.  
Port 1: só `gamely_key_set(1, name, pressed)`, sem Lua.

### Ciclo de vida — antes de `native_callback_init`

Antes de chamar `native_callback_init`, iterar todas as entradas da classe ativa e chamar `native_callback_key(name, false)` para cada uma. Garante que o frontend receba o estado inicial "não pressionado" para todos os botões conhecidos.

---

## `Frontend_Libretro/inputs.c`

```c
if (port > 1 || device != RETRO_DEVICE_JOYPAD) return 0;
// ...switch id → name...
return libretro_key_get(port, name);
```

Subscreve via `gamely_daemon_input_subscribe(on_key, usr)` e mantém estado interno `bool pressed[4][32]` indexado por nome de botão (busca linear, conjunto pequeno). `retro_input_state` consulta esse estado local — sem acesso a `gecnd_*`.

Ports 0–3 disponíveis; port 0 = IR (player 1), ports 1–3 = futuro uso (gamepads, etc.).

---

## `gecnd_t` — campo `input`

```c
const char *input;   // URI do input ativo; default "void://0"
```

Definido em `gecnd_new()` com valor `"void://0"`. Atualizado por `--input` ou por `[args].input` do TOML. Classe `0` = nenhuma.

---

## Daemon_Inputs: pipeline e API

### Fase de montagem — chamada por `set_toml.c`

```c
void gamely_daemon_input_add_class(const char *name);
void gamely_daemon_input_add_keycode(const char *key_name, uint32_t hex);
```

`add_class` registra uma nova classe e define-a como "classe corrente".  
`add_keycode` adiciona `{hex, name}` à classe corrente, inserindo ordenado por `code` (mantém array para bsearch).  
Múltiplos `add_keycode` com o mesmo `key_name` produzem entradas distintas (um botão pode ter vários hexadecimais).

### Fase de ativação — chamada na inicialização das daemons

```c
bool gamely_daemon_input_open(const char *uri);
void gamely_daemon_input_close(void);
```

`open` parseia `uri` (protocolo, nome da classe, device, debug flag), valida protocolo (erro se desconhecido) e nome da classe (erro se não registrado), depois abre o driver com `(port=0, device, km)`.  
Se `debug=0`: descarta todas as classes não selecionadas.  
Se `debug=1`: mantém todas as classes registradas para uso no print de diagnóstico.

Chamado de `update.c` (frontend) passando `gly->input` na inicialização das daemons — após todo o processamento de args. A string `gly->input` é usada apenas durante o `open`; não é armazenada internamente pela daemon.

`set_toml.c` faz `strdup` ao setar `gly->input` a partir do TOML (leak intencional por ora).

### Notificação dos drivers — injeção de eventos

```c
// ttl_ms=0 → sem TTL; port determinado pelo open()
void gamely_daemon_input_push(uint32_t code, bool pressed, uint32_t ttl_ms);
// port explícito — para fontes externas como service_rc.c
void gamely_daemon_input_push_name(const char *name, bool pressed, int port, uint32_t ttl_ms);
```

Drivers chamam `push` com o hex bruto; `service_io.c` chama `service_keymap.c` para lookup (bsearch → `const char *name` do bucket), imprime debug internamente se ativo, e enfileira `{name, pressed, port}` com mutex. `gamely_daemon_input_tick` drena a fila na thread principal e dispara os subscribers.  
`push_name` é para drivers que já conhecem o nome do botão diretamente (ex: `service_rc.c`).

TTL: `pressed=true` com `ttl_ms>0` registra expiração interna `(port, name) → expiry`; `pressed=false` cancela expiração pendente.

### Loop principal

```c
void gamely_daemon_input_tick(gecnd_t *gly);   // TTL expiry → push_name(name, false, port, 0)
void gamely_daemon_input_reset_port(int port); // força false em todos os botões do port
```

`gamely_daemon_input_tick` chamado de `update.c` antes de `gecnd_input_poll_events`.  
`gamely_daemon_input_reset_port` chamado por `service_rc.c` no close da conexão.

### Consumo de eventos

```c
typedef void (*gamely_input_key_cb)(const char *name, bool pressed, int port, void *usr);

// subscrição persistente — cb disparado durante gamely_daemon_input_tick()
void gamely_daemon_input_subscribe(gamely_input_key_cb cb, void *usr);
```

`gamely_daemon_input_tick(gly)` é o único ponto de drenagem: processa TTLs expirados e drena a fila, disparando todos os subscribers para cada evento. Executado na thread principal — thread safe por design.

`update.c` faz `subscribe` passando `gly` como `usr`; a callback chama `gecnd_dispatch_key_event(usr, name, pressed, port)`.  
`Frontend_Libretro/inputs.c` faz `subscribe` com seu próprio contexto e mantém estado interno `bool pressed[4][32]` por nome.

### `gamely_keymap_t` (interno — `service_keymap.c` constrói, `service_io.c` consulta)

```c
typedef struct { uint32_t code; const char *name; } gamely_keymap_entry_t; // ponteiro para gamely_keyname_bucket_t

typedef struct {
    gamely_keymap_entry_t *entries; // malloc'd; count = capacidade real; ordenado por code
    int   count;
    int   debug;
    char  name[32]; // inclui '\0'; máx 31 chars úteis
} gamely_keymap_t;
```

Durante `add_keycode`: insere ordenado (realloc por entrada). Após `open`: sem realloc adicional — array já tem exatamente `count` entradas.  
`gamely_keymap_lookup(km, code)` usa `bsearch` sobre `entries[]`.

### Interface interna dos drivers

```c
typedef struct {
    bool (*open)(int port, const char *device);
    void (*close)(int port);
} gamely_input_driver_t;
```

Cada `driver_*.c` exporta `const gamely_input_driver_t gamely_driver_<proto>`.  
Drivers não recebem o keymap — apenas chamam `gamely_daemon_input_push(code, ...)`. O lookup code → name é feito por `service_io.c` via `service_keymap.c`.

---

## WebSocket `/rc` — port e TTL dinâmicos

```
ws://host/rc             → port 0, sem TTL
ws://host/rc?port=1      → port 1, sem TTL   (até port=3)
ws://host/rc?ttl=100     → port 0, TTL 100 ms por evento
ws://host/rc?port=1&ttl=100
```

### `gamely_webserver.h` — campo `query` em `gly_ws_req_t`

```c
typedef struct {
    gly_conn_id_t  conn_id;
    gly_ws_event_t event;
    const char    *data;
    size_t         len;
    const char    *query;   // string após '?' na URL, NULL se ausente
} gly_ws_req_t;
```

### `driver_warmcat.c` (WebServer)

Em `LWS_CALLBACK_ESTABLISHED`, separar path e query do URI retornado por `WSI_TOKEN_GET_URI`:
- Path vai para `find_route`
- Query fica em `ws_session_t.query[64]` e é exposta via `req.query` no `GLY_WS_OPEN`

`driver_warmcat.c` (WebClient): sem alteração — `parse_url` já preserva query no `path`.

### `service_rc.c`

Mapa estático `conn_id → {port, ttl_ms}`, populado no `GLY_WS_OPEN` via parse de `req->query`.

```
GLY_WS_OPEN    → parseia req->query; registra {port, ttl_ms} para conn_id
GLY_WS_CLOSE   → remove entrada; chama gamely_daemon_input_reset_port(port)
GLY_WS_MESSAGE → mapeamento de nomes RC (red→a etc.)
                  gamely_daemon_input_push_name(name, pressed, port, ttl_ms)
```

Chamadas diretas são seguras: lws roda no mesmo loop libuv da thread principal.

---

## Arquivos

### Criar

| Arquivo | Descrição |
|---|---|
| `include/gamely_input.h` | API do daemon (pipeline + open/close) |
| `lib/Daemon_Inputs/service_keymap.c` | registro de classes, URI parsing, open/close, cleanup, lookup |
| `lib/Daemon_Inputs/service_io.c` | queues, TTLs, states, push, tick, reset_port, subscrição, debug inline |
| `lib/Daemon_Inputs/driver_void.c` | |
| `lib/Daemon_Inputs/driver_lirc.c` | |
| `lib/Daemon_Inputs/driver_serial.c` | |
| `lib/Daemon_Inputs/driver_read.c` | |
| `lib/Daemon_Inputs/driver_aui.c` | refactor de `open_aui.c` |
| `lib/Frontend_Core/set_toml.c` | `gamely_set_toml(gly, path)` — parseia TOML, pipeline de classes, strdup `gly->input` quando setar de `[args]`, libera TOML no final |

### Modificar

| Arquivo | Mudança |
|---|---|
| `include/gecnd.h` | `+input` em `gecnd_t`; remove NEC + todas `gecnd_key_*`; mantém `gecnd_dispatch_key_event` (por nome) |
| `include/gamely_webserver.h` | `+query` em `gly_ws_req_t` |
| `lib/Frontend_Input/key_map.c` | remove NEC hardcoded; remove `gecnd_key_from_name` e `gecnd_key_to_name` |
| `lib/Frontend_Core/update.c` | `gecnd_dispatch_key_event` por nome + port; chama `gamely_daemon_input_tick` |
| `lib/Frontend_Core/set_args.c` | remove `--ir-aui`/`--ir-list`; adiciona `--toml`, `--input` |
| `lib/Frontend_Libretro/inputs.c` | subscrição via `gamely_daemon_input_subscribe`; estado local por nome |
| `lib/Daemon_WebServer/driver_warmcat.c` | expõe query string no ESTABLISHED |
| `lib/Daemon_WebServer/service_rc.c` | port/ttl por conn_id; `gamely_daemon_input_reset_port` no close |
| `CMakeLists.txt` | fontes dos drivers |
| `config.toml` | migra keymaps NEC para `[keymap.*]` |

### Deletar

- `lib/Frontend_Input/open_aui.c`
- `lib/Frontend_Input/key_state.c`
- `lib/Frontend_Input/key_queue.c`

---

## Mudanças de API — referência

| Antigo | Novo | Ação |
|---|---|---|
| `gamely_daemon_input_set_class(uri)` + `gamely_daemon_input_open(port)` | `gamely_daemon_input_open(uri)` | Fundidos; porta sempre 0 |
| `gamely_daemon_input_push(code, pressed)` | `gamely_daemon_input_push(code, pressed, port, ttl_ms)` | Assinatura expandida |
| `gamely_daemon_input_push_name(name, pressed)` | `gamely_daemon_input_push_name(name, pressed, port, ttl_ms)` | Assinatura expandida |
| `gecnd_key_tick(gly)` | `gamely_daemon_input_tick(gly)` | Renomeado, movido para daemon |
| `gecnd_key_reset_port(port)` | `gamely_daemon_input_reset_port(port)` | Renomeado, movido para daemon |
| `gecnd_key_set_state(key, pressed)` | — | Removido (interno) |
| `gecnd_key_get_state(key)` | — | Removido (interno) |
| `gecnd_key_set_state_port(port, key, pressed)` | — | Removido (interno) |
| `gecnd_key_get_state_port(port, key)` | — | Removido (libretro usa subscrição) |
| `gecnd_key_set_state_port_ttl(...)` | fundido em `push(..., ttl_ms)` | Removido |
| `gecnd_key_from_name(name)` | — | Removido (IDs não são mais fixos/públicos) |
| `gecnd_key_to_name(key)` | — | Removido (IDs não são mais fixos/públicos) |
| `gamely_input_key_cb(gecnd_key_t, name, port)` | `gamely_input_key_cb(name, pressed, port)` | Sem ID fixo |
| `gecnd_set_btn_state(name, pressed)` | interno em `service_io.c` | Absorvido; não exposto |
| `gecnd_input_poll_events(gly)` | `gamely_daemon_input_tick(gly)` | Substituído; drena fila + TTL + dispara subscribers |

---

## PlantUML

Gerar diagramas `@plantuml` nas docstrings dos arquivos onde o fluxo não é óbvio pela assinatura. Sem comentários redundantes — apenas o que não se deduz lendo o código.

Candidatos essenciais:

| Arquivo | Diagrama |
|---|---|
| `gamely_input.h` | Sequência: driver thread → push → queue → tick → subscribers |
| `service_io.c` | Sequência: tick — TTL expiry path vs queue drain path |
| `service_keymap.c` | Sequência: add_class → add_keycode → open (build → activate) |
| `gamely_keyname_bucket_t` | Estrutura do bucket e relação de ponteiros com entry/state/TTL |

---

## Ordem de implementação

1. `gecnd.h` — port API, TTL, reset, remove NEC
2. `key_state.c`
3. `key_map.c`
4. `key_queue.c`
5. `update.c`
6. `gamely_input.h` + `service_keymap.c`
7. `service_io.c`
8. `driver_void.c`
9. `driver_aui.c`
10. `driver_lirc.c`, `driver_serial.c`, `driver_read.c`
11. `set_toml.c`
12. `set_args.c`
13. `inputs.c` (libretro)
14. `gamely_webserver.h` + `driver_warmcat.c` (WebServer)
15. `service_rc.c`
16. `CMakeLists.txt` + `config.toml`
17. Deletar `open_aui.c`
