# Design: Daemon_IO, Daemon_Img

> Status: implementado — revisão 5  
> Data: 2026-04-20

---

## Visão Geral

Dois daemons principais, cada um independente.  
Todas as funções públicas ficam em `gecnd.h` (mesmo padrão dos daemons web).

```
┌─────────────────────────────────────────────────────────────────┐
│  Daemon_Img  — resolve imagens (URL → ID → textura GPU)         │
│                                                                  │
│  schemas registrados (resolvedores de URL):                      │
│   ""        → Daemon_IO  (path local sem schema)                 │
│   "file://" → Daemon_IO  (path local com schema)                 │
│   "db://"   → Daemon_IO  (blob direto ou redirect de URL)        │
│   "http://" → Daemon_WebClient  (download HTTP)                  │
│   "https://"→ Daemon_WebClient  (download HTTPS)                 │
└──────────────────────────┬──────────────────────────────────────┘
                           │ bytes chegam ao decoder pipeline
                    ┌──────▼──────┐
                    │  Daemon_IO  │  busca/leitura de arquivos (FS)
                    │             │  armazenamento SQLite (DB)
                    │             │  registra schemas em Daemon_Img
                    └─────────────┘
                         ▲
                    gamely_daemon_db_insert_media()
                    gamely_daemon_db_insert_blob()
                    (chamado por plugins: libretro, dvb, …)
```

### Fluxo de imagem — path local

```
Lua: native_image_load("textures/hud.png")
 │
 ▼  ID atribuído imediatamente [state: SEARCHING]
Daemon_Img → schema "" → fs_schema_cb  (service_resolver.c)
 │
gamely_daemon_fs_search(
    paths = { cwd, exe_cwd, NULL },
    files = { "textures/hud", NULL },
    exts  = { ".etc1", ".png", NULL },   ← ETC1 tentado primeiro
    GLY_FS_ONE_CB_ASYNC, on_found_cb, usr)
→ "/app/assets/textures/hud.etc1"
gamely_daemon_fs_read(path, NULL, NULL, on_read_cb, usr)  → bytes async
 │
 ▼  [state: DECODING]
from="etc1", to="etc1" → passthrough automático (use_thread=false)
upload(id, &backend_data, data, len, w, h, release)
 │
 ▼  [state: READY]
```

### Fluxo de imagem — DB com redirect de URL

```
Lua: native_image_load("db://media/url_image?short=hero")
 │
 ▼  ID atribuído imediatamente [state: SEARCHING]
Daemon_Img → schema "db://" → db_schema_cb  (driver_db_sqlite3.c)
 │
gamely_daemon_db_query_uri("db://media/url_image?short=hero", NULL)
    → SELECT url_image FROM media WHERE short='hero'
    → tipo TEXT: "file://assets/hero.png"
 │
Daemon_Img re-despacha "file://assets/hero.png"
    → schema "file://" → fs_schema_cb → bytes
    → decoder["png"]→"rgba" (use_thread=true) → upload
 │
 ▼  [state: READY]
```

### Fluxo de imagem — DB com blob direto

```
Lua: native_image_load("db://blob/data?id=2")
 │
 ▼  ID atribuído imediatamente [state: SEARCHING]
Daemon_Img → schema "db://" → db_schema_cb
 │
gamely_daemon_db_query_uri("db://blob/data?id=2", &out_len)
    → SELECT data, hint FROM blob WHERE id=2
    → tipo BLOB: bytes + hint="etc1"
 │
 ▼  [state: DECODING]
from="etc1", to="etc1" → passthrough automático → upload
 │
 ▼  [state: READY]
```

---

## 1. Daemon_IO

### 1.1 Responsabilidade

Unificação de Daemon_FS (busca e leitura de arquivos) e Daemon_DB (armazenamento SQLite).  
CMake seleciona um driver FS e um driver DB em tempo de build — mesmos nomes de função, sem vtable.

### 1.2 Estrutura de arquivos

```
lib/Daemon_IO/
├── driver_fs_uv.c      # gamely_daemon_fs_* via libuv uv_queue_work (async real)
├── driver_fs_std.c     # gamely_daemon_fs_* via fopen + fila de pendentes (fallback)
├── driver_db_sqlite3.c # gamely_daemon_db_* completo + SQLite + schema "db://"
├── driver_db_stub.c    # gamely_daemon_db_* no-op (build sem SQLite3)
├── service_search.c    # glob/dir search compartilhado; usado pelos dois drivers FS
└── service_resolver.c  # registra schemas "file://" e "" em Daemon_Img
```

> `driver_fs_uv.c` e `driver_fs_std.c` expõem as mesmas funções `gamely_daemon_fs_*`.  
> `driver_db_sqlite3.c` e `driver_db_stub.c` expõem as mesmas funções `gamely_daemon_db_*`.  
> CMake compila um de cada par — não há vtable nem service layer no DB.

### 1.3 Modo de busca e tipos públicos

```c
typedef enum {
    GLY_FS_ONE,            /* sync,  primeiro resultado — a=char**, b=NULL      */
    GLY_FS_ONE_CB,         /* sync,  primeiro resultado — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB,         /* sync,  todos os resultados — a=gamely_fs_cb, b=usr */
    GLY_FS_ONE_CB_ASYNC,   /* async, primeiro resultado — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB_ASYNC,   /* async, todos os resultados — a=gamely_fs_cb, b=usr */
} gamely_fs_mode_t;

typedef void (*gamely_fs_cb)     (const char *path, void *usr);
typedef void (*gamely_fs_read_cb)(uint8_t *data, size_t len, void *usr);
```

### 1.4 API pública — FS (em `gecnd.h`)

```c
void gamely_daemon_fs_start(void *loop);
void gamely_daemon_fs_stop (void);

/* paths[]: dirs ou paths completos; suportam glob (/mnt/*/*/roms).
 * files[]: NULL=paths diretos, ["*"]=lista dir, ["name"]=busca arquivo.
 * exts[] : NULL=sem filtro, [".png",".etc1"]=filtra extensão.
 * a/b    : GLY_FS_ONE→(char**,NULL)  |  demais→(gamely_fs_cb, usr).
 * Retorna 0 se encontrou ≥1 (ou async iniciado), -1 se não. */
int  gamely_daemon_fs_search(const char      **paths,
                              const char      **files,
                              const char      **exts,
                              gamely_fs_mode_t  mode,
                              void             *a,
                              void             *b);

/* on_done=NULL → sync; on_done!=NULL → async via tick(). */
int  gamely_daemon_fs_read  (const char        *path,
                              uint8_t          **out_data,
                              size_t            *out_len,
                              gamely_fs_read_cb  on_done,
                              void              *usr);

void gamely_daemon_fs_tick(void);

/* Registra "file://" e "" em Daemon_Img.
 * Chamar após fs_start() e img_start(). */
void gamely_daemon_io_resolver_start(void);
```

### 1.5 Schema SQL das tabelas padrão

```sql
CREATE TABLE IF NOT EXISTS persistent (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    key   TEXT UNIQUE NOT NULL,
    value TEXT
);

CREATE TABLE IF NOT EXISTS media (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    name      TEXT,
    short     TEXT UNIQUE,
    url       TEXT,
    type      TEXT,
    url_image TEXT
);

CREATE TABLE IF NOT EXISTS blob (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    data BLOB NOT NULL,
    hint TEXT
);
```

### 1.6 API pública — DB (em `gecnd.h`)

```c
void    gamely_daemon_db_start(void);
void    gamely_daemon_db_stop (void);

int32_t gamely_daemon_db_insert_media(const char *name,
                                       const char *short_id,
                                       const char *url,
                                       const char *type,
                                       const char *url_image);
void    gamely_daemon_db_delete_media(const char *short_id);

int32_t gamely_daemon_db_insert_blob(const uint8_t *data,
                                      size_t         len,
                                      const char    *hint);
void    gamely_daemon_db_delete_blob(int32_t id);

void        gamely_daemon_db_kv_set(const char *key, const char *value);
const char *gamely_daemon_db_kv_get(const char *key);

/* Parses db://table/field?k=v[&k=v…].
 * TEXT  → char*     (caller frees)
 * BLOB  → uint8_t*  (caller frees; *out_len preenchido)
 * INTEGER → int64_t* (caller frees)
 * NULL se não encontrado. */
void *gamely_daemon_db_query_uri(const char *uri, size_t *out_len);
```

> `driver_db_sqlite3.c` registra o schema `"db://"` em `gamely_daemon_db_start()`.
> O schema handler re-despacha TEXT como redirect; entrega BLOB diretamente.

### 1.7 Formato de URI

```
db://<tabela>/<campo>?<col>=<val>[&<col2>=<val2>…]
```

| URI | Query equivalente | Resultado |
|-----|-------------------|-----------|
| `db://media/url_image?short=hero` | `SELECT url_image FROM media WHERE short='hero'` | TEXT → redirect |
| `db://blob/data?id=2` | `SELECT data FROM blob WHERE id=2` | BLOB + hint |
| `db://persistent/value?key=foo` | `SELECT value FROM persistent WHERE key='foo'` | TEXT |

---

## 2. Daemon_Img

### 2.1 Responsabilidade

- Array fixo `url → entry` (id, url, state, fmt, w, h, backend_data, error_msg).
- **ID atribuído imediatamente** na primeira chamada com uma URL, antes de qualquer I/O.
- Carga assíncrona via schemas plugáveis — **sem callbacks de load**, somente polling via `get_state(id)`.
- Decoders e backends totalmente dinâmicos — nenhum formato hardcoded.
- `backend_data` por imagem: ponteiro opaco que o backend escreve no upload e recebe no draw/unload.

### 2.2 Estrutura de arquivos

```
lib/Daemon_Img/
├── service_img.c       # cache, state machine, dispatch, decode via uv_queue_work
└── driver_spng_uv.c    # decoder PNG via libspng + uv_queue_work (GECND_USE_SPNG)
```

### 2.3 Máquina de estados

```c
typedef enum {
    GLY_IMG_SEARCHING = 0,
    GLY_IMG_DECODING  = 1,
    GLY_IMG_READY     = 2,
    GLY_IMG_ERROR     = 3,
} gamely_img_state_t;
```

```
gamely_daemon_img_get_id("url")
    │
    └─ entry{id, state=SEARCHING, url="url"}
          │
          ▼
    SEARCHING ──(bytes chegaram)──► DECODING ──(upload GPU ok)──► READY
         │                              │
         └──(não encontrado)──► ERROR   └──(decode falhou)──► ERROR
```

| Função | SEARCHING/DECODING | READY | ERROR |
|--------|--------------------|-------|-------|
| `get_state(id)` | SEARCHING ou DECODING | READY | ERROR |
| `get_mensure(id, &w, &h)` | 0, 0 | w, h | 0, 0 |
| `get_error(id)` | NULL | NULL | "msg" |
| `draw(id, x, y)` | no-op | desenha | no-op |
| `unload_id/url` | libera | libera | **obrigatório** |

### 2.4 API pública (em `gecnd.h`)

```c
void gamely_daemon_img_start(void *loop);
void gamely_daemon_img_stop (void);

void gamely_daemon_img_register_schema (const char *prefix,
                                         gamely_img_schema_cb  cb, void *usr);
void gamely_daemon_img_register_decoder(const char *from, const char *to,
                                         bool use_thread, gamely_img_decoder_cb cb);
void gamely_daemon_img_register_backend(const char *fmt,
                                         const gamely_img_backend_t *cbs);

/* Retorna ID para url. Inicia load async na primeira chamada.
 * O ID permanece válido até unload ser chamado. */
int32_t            gamely_daemon_img_get_id     (const char *url);
gamely_img_state_t gamely_daemon_img_get_state  (int32_t id);
const char        *gamely_daemon_img_get_error  (int32_t id);
void               gamely_daemon_img_get_mensure(int32_t id, int16_t *w, int16_t *h);
void               gamely_daemon_img_draw       (int32_t id, int16_t x, int16_t y);
void               gamely_daemon_img_unload_id  (int32_t id);
void               gamely_daemon_img_unload_url (const char *url);
void               gamely_daemon_img_unload_all (void);
void               gamely_daemon_img_tick       (void);
```

### 2.5 Decoder — `(from, to, use_thread, cb)`

```c
typedef struct {
    uint8_t *pixels;
    int16_t  w, h;
} gamely_img_decoded_t;

typedef gamely_img_decoded_t (*gamely_img_decoder_cb)(const uint8_t *data, size_t len);
```

- `from==to`: passthrough automático (memdup interno) — não precisa registrar decoder.
- `use_thread=true`: decoder rodado em `uv_queue_work`; upload volta na main thread.

### 2.6 Schema callback (fetch assíncrono)

```c
typedef void (*gamely_img_on_fetch_cb)(
    const uint8_t *data, size_t len, const char *hint, void *usr
);

typedef void (*gamely_img_schema_cb)(
    const char *url, void *schema_usr,
    gamely_img_on_fetch_cb on_done, void *on_done_usr
);
```

**Redirect**: chamar `on_done(NULL, 0, redirect_url, usr)` — daemon re-despacha com nova URL.

### 2.7 Backend registration

```c
typedef void (*gamely_img_release_cb)(void *ptr);

typedef void (*gamely_img_upload_cb)(
    int32_t id, void **backend_data,
    const uint8_t *data, size_t len,
    int16_t w, int16_t h,
    gamely_img_release_cb release
);

typedef struct {
    gamely_img_upload_cb upload;
    void (*draw)      (int32_t id, void *backend_data, int16_t x, int16_t y);
    void (*unload)    (int32_t id, void *backend_data);
    void (*unload_all)(void);
} gamely_img_backend_t;
```

- `*backend_data`: o backend escreve seu handle GPU aqui no upload; o daemon o passa em draw/unload.
- `release(data)`: backend chama para liberar o buffer de pixels (sync → chama na hora).
- Último backend registrado para um `fmt` tem maior prioridade.

---

## 3. Backend_OpenGL

### 3.1 Estrutura

```
lib/Backend_OpenGL/render/
└── image.c    # gamely_img_backend_t para "rgba" usando atlas existente (ge_atlas_alloc)
```

### 3.2 Funcionamento

`gl_upload`: aloca slot no atlas (`ge_atlas_alloc`), faz `glTexSubImage2D`, heap-alloca `GLTexture*`
como `backend_data`, chama `release(data)`.

`gl_draw`: casta `backend_data` para `GLTexture*`, chama `ge_batch_add_vertex_tex` ×6 com half-pixel UV.

`gl_unload`: `free(backend_data)`.

`gl_unload_all`: no-op (atlas persiste; entradas liberadas via `gl_unload`).

```c
/* Registra o backend OpenGL "rgba" em Daemon_Img.
 * Chamar após gamely_daemon_img_start(). */
void gamely_daemon_img_opengl_register(void);
```

---

## 4. Injeção de schemas em Daemon_Img

Daemon_Img não conhece nenhuma fonte de dados diretamente.  
Cada módulo registra seu handler durante o próprio init.

```c
/* Daemon_IO/service_resolver.c — paths locais */
gamely_daemon_io_resolver_start();   /* registra "file://" e "" */

/* Daemon_IO/driver_db_sqlite3.c — chamado internamente em gamely_daemon_db_start() */
/* registra "db://" automaticamente */

/* Daemon_WebClient/service_img_resolver.c */
gamely_daemon_webclient_img_register();   /* registra "http://" e "https://" */

/* Backend_OpenGL/render/image.c */
gamely_daemon_img_opengl_register();      /* registra backend "rgba" */

/* Daemon_Img/driver_spng_uv.c */
gamely_daemon_img_spng_register();        /* registra decoder "png"→"rgba" */
```

---

## 5. Daemon_WebClient — service_img_resolver.c

```
lib/Daemon_WebClient/
└── service_img_resolver.c   # acumula response HTTP; registra "http://" e "https://"
```

`gamely_daemon_webclient_img_register()` — chamar após `webclient_start()` e `img_start()`.  
Hint derivado da extensão da URL (ex.: `.png` → `"png"`).

---

## 6. Frontend_Api

### 6.1 image.c

Thin wrapper Lua sobre Daemon_Img. Sem khash privado.

```c
/* aceita string (URL) ou integer (ID) */
static int32_t resolve_id(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING)
        return gamely_daemon_img_get_id(luaL_checkstring(L, idx));
    return (int32_t)luaL_checkinteger(L, idx);
}
```

| Lua | Delega para |
|-----|-------------|
| `native_image_load(url)` → `id` | `gamely_daemon_img_get_id` |
| `native_image_exists(url\|id)` → `bool` | `get_state(id) == GLY_IMG_READY` |
| `native_image_error(url\|id)` → `str\|nil` | `gamely_daemon_img_get_error` |
| `native_image_mensure(url\|id)` → `w, h` | `gamely_daemon_img_get_mensure` |
| `native_image_draw(url\|id, x, y)` | `gamely_daemon_img_draw` |
| `native_image_unload(url\|id)` | `unload_url` ou `unload_id` |
| `native_image_unload_all()` | `gamely_daemon_img_unload_all` |

### 6.2 storage.c

Thin wrapper Lua sobre `gamely_daemon_db_kv_set/get`.  
Substitui `ThirdParty_Api/storage.c` (removido).

---

## 7. Estrutura de diretórios

```
lib/
├── Daemon_IO/
│   ├── driver_fs_uv.c      # FS async via libuv
│   ├── driver_fs_std.c     # FS sync via fopen
│   ├── driver_db_sqlite3.c # DB completo com SQLite3
│   ├── driver_db_stub.c    # DB no-op (sem SQLite3)
│   ├── service_search.c    # glob/dir search compartilhado
│   └── service_resolver.c  # schemas "file://" e ""
├── Daemon_Img/
│   ├── service_img.c       # cache, state machine, decode
│   └── driver_spng_uv.c    # decoder PNG via libspng
├── Daemon_WebClient/
│   └── service_img_resolver.c  # schemas "http://" e "https://"
├── Backend_OpenGL/render/
│   └── image.c             # backend "rgba" via atlas
├── Common_Utils/
│   ├── uri.c               # gly_uri_* (schema, host, path, query)
│   └── uri.h
└── Frontend_Api/
    ├── image.c             # wrapper Lua sobre Daemon_Img
    └── storage.c           # wrapper Lua sobre gamely_daemon_db_kv_set/get

include/
└── gecnd.h                 # todas as funções públicas
```

**Removidos:**
- `lib/Daemon_FS/` — fundido em `Daemon_IO/`
- `lib/Daemon_DB/` — fundido em `Daemon_IO/`
- `lib/ThirdParty_Api/storage.c` — substituído por `Frontend_Api/storage.c`
- Hooks de imagem em `gehook.h`: `native_image_load/draw/mensure/unload/unload_all`

---

## 8. Decisões fechadas

| Item | Decisão |
|------|---------|
| Daemon_FS + Daemon_DB | Unificados em `Daemon_IO/` com prefixo `driver_fs_*` / `driver_db_*` |
| Service layer no DB | Removido — drivers implementam a API pública diretamente |
| Headers de driver | Não criados — protótipos declarados no arquivo que chama |
| Load callback | Removido — somente `get_id(url)` + polling via `get_state(id)` |
| `gamely_daemon_db_query_uri` | API pública (não interna) |
| `unload` | Três funções: `unload_id`, `unload_url`, `unload_all` |
| `backend_data` por imagem | `void*` heap-alocado pelo backend; daemon guarda e repassa |
| `release(data)` | Backend chama para liberar buffer de pixels (sync = na hora) |
| Redirect | `on_fetch(NULL, 0, redirect_url, usr)` — daemon re-despacha |
| Passthrough `from==to` | Automático no daemon, sem registrar decoder |
| `use_thread` | Estático no registro do decoder — sem flag probe |
| SDL2 / Raylib backends | Fora do escopo atual |
| CMakeLists.txt | Não modificado nesta sessão |
