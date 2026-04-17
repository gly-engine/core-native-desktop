# Plano: múltiplos inputs + port + service_remote

## Resumo das mudanças

5 partes. As 3 primeiras mexem na arquitetura do `Daemon_Inputs`; as 2 últimas são novas features.

---

## 1. Nova API: `add_source` + `open()` sem argumento

### Mudança de assinatura

```c
// ANTES
bool gamely_daemon_input_open(const char *uri);

// DEPOIS
void gamely_daemon_input_add_source(const char *uri);  /* nome a confirmar */
bool gamely_daemon_input_open(void);
```

### Comportamento

`gamely_daemon_input_add_source(uri)` — chamado N vezes (uma por `--input`):
- Parseia o URI (proto, classname, port, device, debug)
- Armazena em `g_reg.sources[n]` (novo array, max ~8)
- **Não** abre o driver ainda

`gamely_daemon_input_open()` — chamado uma vez após todos os `add_source`:
- Para cada source registrado: encontra o keymap, abre o driver
- Se **nenhum** source tiver `?debug=1`: libera keymaps não usados por nenhuma source
- Retorna `false` se qualquer source falhar

### Estrutura interna nova em `service_keymap.c`

```c
typedef struct {
    char                         proto[16];
    char                         classname[32];
    char                         device[256];
    int                          port;
    int                          debug;
    const gamely_input_driver_t *driver;
    gamely_keymap_t             *active;
} gamely_input_source_t;

#define MAX_SOURCES 8
```

`g_reg` ganha `sources[MAX_SOURCES]` e `source_count`. Os campos antigos `proto`, `device`, `debug`, `port`, `driver`, `active` saem do topo do `g_reg` e vão para dentro de cada source.

`gamely_keymap_get_port()` e `gamely_keymap_get_active()` continuam existindo mas precisam de uma forma de saber "qual source está chamando agora" — ver **Limitação** abaixo.

### Limpeza de keymaps

Atual: libera tudo que não é `g_reg.active` se `debug==0`.

Novo: libera qualquer classe não referenciada por nenhuma `source.active`, desde que nenhuma source tenha `debug=1`.

### Impacto em `set_args.c`

Opção A (simples): `--input` chama `gamely_daemon_input_add_source(opt.arg)` diretamente, sem guardar em `gly->input`.

Opção B: continua guardando em `gly->input` mas agora como lista. Mais invasivo.

**Recomendação:** Opção A — `--input` pode aparecer múltiplas vezes no mesmo argv e cada ocorrência chama `add_source`.

### Limitação atual (sem tocar em `driver_aui.c`)

`gamely_daemon_input_push` ainda usa `gamely_keymap_get_port()` + `gamely_keymap_get_active()`, que retornam dados de uma única source. Com múltiplas sources de **drivers diferentes** (ex: `aui + lirc`) isso funciona se cada driver guarda sua própria port internamente. Com **duas sources do mesmo driver** (ex: dois `aui://`) haveria ambiguidade de porta — isso fica como trabalho futuro quando a assinatura do driver mudar.

---

## 2. URI `?port=` em `service_keymap.c`

Sem mudança em relação ao plano anterior. No loop de query params de `gamely_daemon_input_open` (agora `add_source`):

```c
} else if (strncmp(p, "port=", 5) == 0) {
    src->port = (int)strtol(p + 5, NULL, 10);
}
```

Arquivo: `lib/Daemon_Inputs/service_keymap.c`

---

## 3. `service_rc.c` — porta por cliente via número WS

Protocolo novo:
```
+key   → press na porta atual do cliente (default 0)
-key   → release na porta atual do cliente
"0"    → muda porta deste cliente para 0
"1"    → muda porta deste cliente para 1
```

Implementação: mapa estático `conn_id → port` em `service_rc.c`:

```c
#define RC_MAX_CLIENTS 16
static struct { uint32_t id; int port; } g_rc_ports[RC_MAX_CLIENTS];
```

- `GLY_WS_OPEN` → insere com `port=0`
- `GLY_WS_CLOSE` → remove entrada
- `GLY_WS_MESSAGE` → se só dígitos: atualiza porta; senão: processa `+/-key` com a porta do cliente

Arquivo: `lib/Daemon_WebServer/service_rc.c`

---

## 4. `--remote` + `service_remote.c` — propagador de inputs

### `set_args.c`

```c
{ "remote", ko_required_argument, 503 },
// ...
if (c == 503) { gamely_daemon_input_remote(opt.arg); }
```

### `lib/Daemon_Inputs/service_remote.c`

Assina inputs locais e os envia para um servidor WebSocket remoto.

**Protocolo gerado** (mesmo que `service_rc.c` entende):
```
porta mudou → envia "N"
press       → envia "+key"
release     → envia "-key"
```

**Arquitetura do cliente WS:** reusar o contexto lws existente do webserver via `lws_client_connect_via_info`. O `gamely_daemon_input_remote(url)` armazena a URL; a conexão é criada no primeiro `tick()` do webserver.

**Dúvidas ainda abertas:**
- Reconexão automática sim/não?
- Funciona sem `--port` (sem webserver local)?

---

## Ordem de implementação

1. `add_source` + refatoração de `g_reg` em `service_keymap.c`
2. `?port=` parsing (entra junto com o item 1)
3. Atualizar `set_args.c` (`--input` → `add_source`)
4. `service_rc.c` porta por cliente
5. `service_remote.c` + `--remote`

---

## Dúvidas abertas

1. **Nome da função:** `gamely_daemon_input_add_source`? `add_input`? `add_uri`?
2. **`--remote`**: reconexão automática?
3. **`--remote`**: funciona sem `--port`?
4. **`RC_MAX_CLIENTS`**: 16 é suficiente?
