# Design: Daemon_FS, Daemon_DB e Daemon_Img

> Status: proposta de design — revisão 3  
> Data: 2026-04-19

---

## Visão Geral

Três novos daemons, cada um independente.  
Todas as funções públicas ficam em `gecnd.h` (mesmo padrão dos daemons web).

```
┌─────────────────────────────────────────────────────────────────┐
│  Daemon_Img  — resolve imagens (URL → ID → textura GPU)         │
│                                                                  │
│  schemas registrados (resolvedores de URL):                      │
│   ""        → Daemon_FS  (path local sem schema)                 │
│   "file://" → Daemon_FS  (path local com schema)                 │
│   "db://"   → Daemon_DB  (blob direto ou redirect de URL)        │
│   "http://" → webclient  (download HTTP)                         │
└──────────────────────────┬──────────────────────────────────────┘
                           │ bytes chegam ao decoder pipeline
                    ┌──────▼──────┐
                    │  Daemon_FS  │  busca e leitura de arquivos
                    └─────────────┘
                    ┌──────▼──────┐
                    │  Daemon_DB  │  armazenamento SQLite
                    │             │  plugins inserem dados,
                    │             │  referenciam via db://
                    └─────────────┘
                         ▲
                    gamely_daemon_db_insert_media()
                    gamely_daemon_db_insert_blob()
                    (chamado por plugins: libretro, dvb, …)

┌─────────────────────────────────────────────────────────────────┐
│  Daemon_Media  — INDEPENDENTE  (mídias contínuas: vídeo,        │
│                  rádio, jogos)                                   │
│  registra players: "libretro://", "dvb://", "http://", …        │
│  → fora do escopo atual                                          │
└─────────────────────────────────────────────────────────────────┘
```

### Fluxo de imagem — path local

```
Lua: native_image_load("textures/hud.png")
 │
 ▼  ID atribuído imediatamente [state: SEARCHING]
Daemon_Img → schema "" → fs_schema_cb
 │
gamely_daemon_fs_search(
    paths = { cwd, exe_cwd, NULL },
    files = { "textures/hud", NULL },
    exts  = { ".etc1", ".png", NULL },   ← ETC1 tentado primeiro
    GLY_FS_ASYNC | GLY_FS_CALLBACK, …)
→ "/app/assets/textures/hud.etc1"
gamely_daemon_fs_read(path, GLY_FS_ASYNC, …)  → bytes
 │
 ▼  [state: DECODING]
from="etc1", to="etc1" → passthrough automático (use_thread=false)
upload_cb("etc1", id, data, len, w, h, release_cb)
 │
 ▼  [state: READY]
```

### Fluxo de imagem — DB com redirect de URL

```
Lua: native_image_load("db://media/url_image?short=hero")
 │
 ▼  ID atribuído imediatamente [state: SEARCHING]
Daemon_Img → schema "db://" → db_schema_cb
 │
Daemon_DB resolve("db://media/url_image?short=hero")
    → SELECT url_image FROM media WHERE short='hero'
    → tipo TEXT: "file://assets/hero.png"
 │
Daemon_Img re-despacha "file://assets/hero.png"
    → schema "file://" → fs_schema_cb → bytes
    → decoder["png"]→"rgba" (use_thread=true) → upload_cb
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
Daemon_DB resolve("db://blob/data?id=2")
    → SELECT data, hint FROM blob WHERE id=2
    → tipo BLOB: bytes + hint="etc1"
 │                              ← bytes vão direto ao decoder, sem redirect
 ▼  [state: DECODING]
from="etc1", to="etc1" → passthrough automático
upload_cb("etc1", id, data, len, w, h, release_cb)
 │
 ▼  [state: READY]
```

---

## 1. Daemon_FS

### 1.1 Responsabilidade

Busca de arquivos em múltiplos diretórios/extensões e leitura de bytes.  
Dois drivers: `driver_uv.c` (libuv, async real) e `driver_std.c` (fallback POSIX).  
Não conhece Daemon_Img nem Daemon_Media — eles é que injetam o FS como handler de `file://`.

Será amplamente usado pelo futuro **`plugins/libretro/`** (scanner de ROMs, shaders, assets).

### 1.2 Estrutura de arquivos

```
lib/Daemon_FS/
├── service_fs.c      # API pública, despacho para driver ativo
├── driver_uv.c       # uv_fs_scandir + uv_fs_open/read  (com libuv)
└── driver_std.c      # opendir/fopen                    (fallback)
```

### 1.3 Modo de busca e tipos públicos

```c
/* Modo de operação — enum fechado, sem combinações inválidas. */
typedef enum {
    GLY_FS_ONE,            /* sync,  primeiro resultado — a=char**, b=NULL      */
    GLY_FS_ONE_CB,         /* sync,  primeiro resultado — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB,         /* sync,  todos os resultados — a=gamely_fs_cb, b=usr */
    GLY_FS_ONE_CB_ASYNC,   /* async, primeiro resultado — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB_ASYNC,   /* async, todos os resultados — a=gamely_fs_cb, b=usr */
} gamely_fs_mode_t;

/* Callback chamada por resultado encontrado. */
typedef void (*gamely_fs_cb)(const char *path, void *usr);

/* Callback de leitura assíncrona — data heap-alocado; caller faz free. */
typedef void (*gamely_fs_read_cb)(uint8_t *data, size_t len, void *usr);
```

### 1.4 API pública (em `gecnd.h`)

```c
/* ── Lifecycle ──────────────────────────────────────────────────────────
 * loop: ponteiro uv_loop_t* — obrigatório para operações async.         */
void gamely_daemon_fs_start(void *loop);
void gamely_daemon_fs_stop (void);

/* ── Search ─────────────────────────────────────────────────────────────
 *
 * paths[]: diretórios ou caminhos completos (NULL-terminated). Obrigatório.
 * files[]: nomes de arquivo sem extensão (NULL = usa paths como estão).
 * exts[] : extensões a tentar, em ordem (NULL = sem filtro de extensão).
 * mode   : gamely_fs_mode_t — define sync/async e como entregar resultado.
 * a      : GLY_FS_ONE → char**  |  demais → gamely_fs_cb
 * b      : GLY_FS_ONE → NULL    |  demais → usr
 *
 * Retorna 0 em sucesso (ou operação async iniciada), -1 em erro.
 * -------------------------------------------------------------------- */
int gamely_daemon_fs_search(
    const char      **paths,
    const char      **files,
    const char      **exts,
    gamely_fs_mode_t  mode,
    void             *a,
    void             *b
);

/* ── Read ───────────────────────────────────────────────────────────────
 * on_done=NULL + out_data/out_len → sync (bloqueia).
 * on_done != NULL                 → async via tick().                   */
int gamely_daemon_fs_read(
    const char        *path,
    uint8_t          **out_data,
    size_t            *out_len,
    gamely_fs_read_cb  on_done,
    void              *on_done_usr
);

/* Drena fila de callbacks async — chamado pela main loop. */
void gamely_daemon_fs_tick(void);
```

### 1.5 Semântica de `paths`, `files` e `exts`

```
paths[]:  diretórios ou caminhos completos. Suportam glob: "/mnt/*/*/roms".
          O daemon expande os wildcards antes de buscar.

files[]:  NULL  → paths[] são usados como caminhos completos/diretos (stat/exist).
          ["*"] → lista TODOS os arquivos dentro dos paths[] (glob *).
          ["nome"] → procura "nome" dentro dos paths[] (+ exts se fornecidas).

exts[]:   NULL  → sem filtro de extensão.
          [".nes",".sfc"] → filtra por extensão na iteração.

Retorno: 0 = encontrou ≥1 resultado (ou busca async iniciada), -1 = não encontrou / erro.
GLY_FS_SET_VALUE implica sync — não combinável com GLY_FS_ASYNC.
```

### 1.6 Exemplos de uso

```c
/* 1. Sync, primeiro resultado, set_value */
char *found = NULL;
int ok = gamely_daemon_fs_search(
    (const char*[]){ "/app/assets", cwd, NULL },
    (const char*[]){ "textures/hud", NULL },
    (const char*[]){ ".etc1", ".png", NULL },
    GLY_FS_ONE,
    &found, NULL);
/* ok=0 → found="…/textures/hud.etc1";  ok=-1 → não encontrou */

/* 2. Async, primeiro resultado, callback */
gamely_daemon_fs_search(
    paths, files, exts,
    GLY_FS_ONE_CB_ASYNC,
    my_cb, usr);

/* 3. Async, TODOS os resultados — scan de ROMs (substitui scanner.c) */
gamely_daemon_fs_search(
    (const char*[]){ "/mnt/*/*/roms", "/mnt/*/*/libretro/roms", NULL },
    (const char*[]){ "*", NULL },                    /* todos os arquivos */
    (const char*[]){ ".nes", ".sfc", ".gba", NULL }, /* filtro de extensão */
    GLY_FS_ALL_CB_ASYNC,
    rom_found_cb, usr);

/* 4. Sync, localizar core libretro com variantes de nome */
gamely_daemon_fs_search(
    (const char*[]){ cwd, exedir, "/mnt/*/*/libretro", NULL },
    (const char*[]){ "snes9x_libretro.so", "libsnes9x_libretro.so", NULL },
    NULL,
    GLY_FS_ONE,
    &core_path, NULL);
```

---

## 2. Daemon_DB

### 2.1 Responsabilidade

Banco SQLite central do sistema. Absorve e estende o `ThirdParty_Api/storage.c` existente.

- Mantém tabelas padrão: **`persistent`** (chave-valor Lua), **`media`** (catálogo de mídias), **`blob`** (bytes brutos por plugins).
- **O DB nunca cacheia por conta própria** — são os plugins que inserem dados via `insert_media` / `insert_blob` e depois os referenciam como URLs `db://`.
- Resolve URIs `db://<tabela>/<campo>?<where>` com dois comportamentos:
  - Coluna TEXT → devolve a URL para Daemon_Img re-despachar pelo schema correto.
  - Coluna BLOB → devolve bytes brutos + hint direto ao decoder pipeline.
- Registra-se em Daemon_Img como handler de `"db://"` durante o próprio init.
- Não conhece Daemon_FS, Daemon_Media nem Daemon_Img além do `register_schema`.

### 2.2 Estrutura de arquivos

```
lib/Daemon_DB/
└── service_db.c      # init SQLite, tabelas padrão, insert/delete APIs,
                      # resolve interno, + absorve lógica de storage.c
```

> SQLite é dependência direta (já usada em `storage.c`). Não há driver condicional.

### 2.3 Schema SQL das tabelas padrão

```sql
-- chave-valor persistente (Lua) — substitui ThirdParty_Api/storage.c
CREATE TABLE IF NOT EXISTS persistent (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    key   TEXT UNIQUE NOT NULL,
    value TEXT
);

-- catálogo de mídias — populado por plugins (DVB, libretro, …)
CREATE TABLE IF NOT EXISTS media (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    name      TEXT,
    short     TEXT UNIQUE,   -- identificador curto: "5.1", "hero", …
    url       TEXT,          -- "rtsp://…", "file://…", "db://blob/data?id=N"
    type      TEXT,          -- "video", "audio", "image", …
    url_image TEXT           -- URL da thumbnail/preview
);

-- cache de blobs brutos — populado por plugins
CREATE TABLE IF NOT EXISTS blob (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    data BLOB NOT NULL,
    hint TEXT              -- "etc1", "png", "h264", …
);
```

### 2.4 Formato de URI

```
db://<tabela>/<campo>?<col>=<val>[&<col2>=<val2>…]
```

| URI | Query equivalente | Resultado |
|-----|-------------------|-----------|
| `db://media/url?short=5.1` | `SELECT url FROM media WHERE short='5.1'` | TEXT → redirect |
| `db://blob/data?id=2` | `SELECT data FROM blob WHERE id=2` | BLOB + hint |
| `db://media/url_image?short=5.1` | `SELECT url_image FROM media WHERE short='5.1'` | TEXT → redirect |

### 2.5 API pública (em `gecnd.h`)

```c
/* ── Lifecycle ──────────────────────────────────────────────────────────
 * DB é síncrono — não precisa de loop, mas segue o padrão dos outros. */
void gamely_daemon_db_start(void);
void gamely_daemon_db_stop (void);

/* ── Catálogo de mídia ──────────────────────────────────────────────── */

/* Insere ou atualiza uma entrada de mídia.
 * Usado por plugins: DVB (proprietário), libretro (público), etc.
 * Retorna o id gerado. */
int32_t gamely_daemon_db_insert_media(
    const char *name,       /* nome exibível          */
    const char *short_id,   /* "5.1", "hero", …       */
    const char *url,        /* src real ou db://…     */
    const char *type,       /* "video", "image", …    */
    const char *url_image   /* thumbnail, pode ser NULL */
);

void gamely_daemon_db_delete_media(const char *short_id);

/* ── Cache de blobs ─────────────────────────────────────────────────── */

/* Insere bytes brutos; retorna id para uso em "db://blob/data?id=<N>". */
int32_t gamely_daemon_db_insert_blob(
    const uint8_t *data, size_t len,
    const char    *hint   /* "etc1", "png", … — pode ser NULL */
);

void gamely_daemon_db_delete_blob(int32_t id);

/* ── Chave-valor persistente (para Lua / storage.c) ────────────────── */
void        gamely_daemon_db_kv_set(const char *key, const char *value);
const char *gamely_daemon_db_kv_get(const char *key);   /* retorna NULL se ausente */
```

> `gamely_daemon_db_resolve()` é **interno** — registrado como handler `"db://"` em
> Daemon_Img e Daemon_Media durante o init de `service_db.c`.

### 2.6 Comportamento interno — BLOB vs TEXT

```
gamely_daemon_db_resolve("db://media/url?short=5.1")
    │
    ├─ SELECT url FROM media WHERE short='5.1'
    │
    ├─ sqlite3_column_type == SQLITE_TEXT
    │     → devolve redirect_url = "db://blob/data?id=2"
    │     → daemon chamador re-despacha para "db://blob/data?id=2"
    │
    └─ (se fosse BLOB) → devolve (data, len, hint) diretamente

gamely_daemon_db_resolve("db://blob/data?id=2")
    │
    ├─ SELECT data, hint FROM blob WHERE id=2
    └─ sqlite3_column_type == SQLITE_BLOB
          → devolve (data, len, hint="etc1") ao daemon chamador
```

---

## 3. Daemon_Img

### 3.1 Responsabilidade

- Hashmap `URL → entry` (id, fmt, w, h, atlas, state, error_msg).
- **ID atribuído imediatamente** na primeira chamada com uma URL, mesmo antes de qualquer I/O.
- Carga assíncrona via schemas plugáveis.
- Decoders plugáveis com suporte a threads por imagem via libuv (`uv_queue_work`).
- Preferência automática por formatos comprimidos (ETC1/ETC2).
- Dois atlases GPU: **RGBA** e **Compressed**.
- Máquina de estados por imagem.

### 3.2 Estrutura de arquivos

```
lib/Daemon_Img/
├── service_img.c       # cache, state machine, dispatch, fila async
├── service_atlas.c     # atlas RGBA + atlas Compressed
└── driver_spng_uv.c    # decoder PNG via libspng + uv_queue_work (GECND_USE_SPNG)
```

### 3.3 Formatos

```c
typedef enum {
    GLY_IMG_FMT_UNKNOWN  = 0,
    GLY_IMG_FMT_RGBA8888 = 1,   /* atlas RGBA       */
    GLY_IMG_FMT_RGB565   = 2,   /* atlas RGBA       */
    GLY_IMG_FMT_ETC1     = 3,   /* atlas Compressed, sem decode CPU */
    GLY_IMG_FMT_ETC2     = 4,   /* atlas Compressed, sem decode CPU */
} gamely_img_fmt_t;
```

### 3.4 Máquina de estados

ID é atribuído na primeira chamada. Estado inicial: `GLY_IMG_SEARCHING`.

```c
typedef enum {
    GLY_IMG_SEARCHING = 0,   /* aguardando bytes (FS, HTTP, DB…) */
    GLY_IMG_DECODING  = 1,   /* bytes prontos, decode em curso   */
    GLY_IMG_READY     = 2,   /* textura na GPU, pronta p/ draw   */
    GLY_IMG_ERROR     = 3,   /* falha (fetch ou decode)          */
} gamely_img_state_t;
```

```
gamely_daemon_img_load_async("url", cb, usr)
    │
    └─ ID atribuído agora (ex.: id=7) → entry{id=7, state=SEARCHING, url="url"}
           │
           ▼
    SEARCHING ──(bytes chegaram)──► DECODING ──(upload GPU ok)──► READY ──► cb(7, usr)
         │                              │
         └──(não encontrado)──► ERROR   └──(decode falhou)──► ERROR
                                                               cb(7, usr)  ← id real
```

**Semântica do estado ERROR:**

| Função | Comportamento em ERROR |
|--------|------------------------|
| `native_image_exists(url_or_id)` | `false` |
| `native_image_mensure(url_or_id)` | `0, 0` |
| `native_image_draw(url_or_id, …)` | no-op, retorna id |
| `native_image_error(url_or_id)` | string descritiva (ex.: `"file not found"`) |
| `native_image_unload(url_or_id)` | **obrigatório** para liberar a entrada do cache |

> Entrada em ERROR ocupa espaço no hashmap até `unload` ser chamado.

### 3.5 Decoder — `(from, to, use_thread, cb)`

Decoder registrado como tripla `from` → `to`. O daemon monta o pipeline automaticamente:
se `from == to` e existe um backend para esse formato, a conversão é zero — o decoder
faz apenas passthrough (copia os bytes). Se precisar converter (ex.: "png" → "rgba"),
o decoder é chamado.

```c
/* Resultado do decoder — sempre aloca pixels (mesmo passthrough = memdup).
 * O daemon libera data original após o retorno do decoder.
 * O daemon libera pixels via release_cb quando o backend confirmar upload. */
typedef struct {
    uint8_t *pixels;   /* alocado pelo decoder; daemon libera via release_cb */
    int16_t  w, h;
} gamely_img_decoded_t;

typedef gamely_img_decoded_t (*gamely_img_decoder_cb)(
    const uint8_t *data,
    size_t         len
);
```

Não há mais flag `probe` — a thread é decidida estaticamente no registro:

```c
/* from:       extensão de origem: "png", "jpg", …
 * to:         formato alvo:       "rgba", "rgb565", …
 * use_thread: true → decoder_cb chamado em uv_queue_work
 * Quando from==to: passthrough automático, não precisa registrar. */
void gamely_daemon_img_register_decoder(
    const char            *from,
    const char            *to,
    bool                   use_thread,
    gamely_img_decoder_cb  cb
);
```

Exemplos de registro:

```c
/* PNG → RGBA, em worker thread */
gamely_daemon_img_register_decoder("png", "rgba", true,  png_decode_cb);

/* JPG → RGBA, em worker thread */
gamely_daemon_img_register_decoder("jpg", "rgba", true,  jpg_decode_cb);

/* ETC1 → ETC1: NÃO precisa registrar — quando from==to o daemon
 * faz passthrough automático (memdup interno, sem chamar decoder). */
```

**Pipeline de decisão por arquivo encontrado:**

```
Arquivo: "hud.etc1"   (backend registrou "etc1")
    └─ from="etc1", to="etc1" → passthrough → upload_cb("etc1", data, …)

Arquivo: "hud.png"    (backend só tem "rgba")
    └─ from="png", to="rgba" → png_decode_cb em worker → upload_cb("rgba", pixels, …)

Arquivo: "hud.png"    (backend tem "rgba" E "etc1", mas não há .etc1 disponível)
    └─ from="png", to="rgba" → png_decode_cb → upload_cb("rgba", …)
```

### 3.6 Schema callback (fetch assíncrono)

```c
/* on_done: chamado quando bytes chegam (qualquer thread).
 * hint: extensão sugerida pelo provider ("etc1", "png", …) ou NULL.
 * callback deve alocar data; daemon faz free depois. */
typedef void (*gamely_img_on_fetch_cb)(
    const uint8_t *data, size_t len, const char *hint, void *usr
);

typedef void (*gamely_img_schema_cb)(
    const char *url, void *schema_usr,
    gamely_img_on_fetch_cb on_done, void *on_done_usr
);
```

### 3.7 Backend registration

O backend registra os formatos que a GPU aceita + os callbacks de operação GPU.  
Daemon_Img não conhece o backend — só chama os callbacks registrados.

```c
/* upload_cb: chamado quando pixels/bytes estão prontos para GPU.
 * release:   função que o backend DEVE chamar quando terminar de usar data
 *            (sincrono → chama na hora; PBO async → chama no fence callback).
 *            Daemon usa release para saber quando pode liberar o buffer. */
typedef void (*gamely_img_release_cb)(void *ptr);

/* upload_cb: backend escreve em *backend_data o handle GPU (ex.: GLuint*).
 *            O daemon guarda esse ponteiro por imagem e o passa nas chamadas
 *            seguintes (draw, unload).
 * release:   backend chama release(data) quando GPU terminar com o buffer.
 *            Sync → chama na hora. PBO async → chama no fence callback. */
typedef void (*gamely_img_upload_cb)(
    int32_t               id,
    void                **backend_data,  /* out: backend escreve seu handle aqui */
    const uint8_t        *data,
    size_t                len,
    int16_t               w,
    int16_t               h,
    gamely_img_release_cb release
);

typedef struct {
    gamely_img_upload_cb upload;
    void (*draw)      (int32_t id, void *backend_data, int16_t x, int16_t y);
    void (*unload)    (int32_t id, void *backend_data);
    void (*unload_all)(void);
} gamely_img_backend_t;

/* fmt: "rgba", "rgb565", "etc1", …
 * Último registrado = maior prioridade na escolha de formato e extensão de busca. */
void gamely_daemon_img_register_backend(
    const char                  *fmt,
    const gamely_img_backend_t  *cbs
);
```

Exemplo — GL sem ETC1:
```c
static void gl_upload_rgba(int32_t id, void **bd, const uint8_t *data,
                            size_t len, int16_t w, int16_t h,
                            gamely_img_release_cb release) {
    GLuint *tex = malloc(sizeof(GLuint));
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);
    *bd = tex;           /* daemon guarda tex* por imagem */
    release(data);       /* sync: libera o buffer agora   */
}

static void gl_draw(int32_t id, void *bd, int16_t x, int16_t y) {
    GLuint tex = *(GLuint*)bd;
    /* draw quad com a textura */
}

static void gl_unload(int32_t id, void *bd) {
    GLuint *tex = (GLuint*)bd;
    glDeleteTextures(1, tex);
    free(tex);
}

static gamely_img_backend_t gl_rgba_backend = {
    .upload = gl_upload_rgba, .draw = gl_draw,
    .unload = gl_unload,      .unload_all = gl_unload_all,
};
gamely_daemon_img_register_backend("rgba", &gl_rgba_backend);

/* Se GL suportar ETC1 (GL_OES_compressed_ETC1_RGB8_texture):
 * registra depois → "etc1" tem maior prioridade que "rgba" */
gamely_daemon_img_register_backend("etc1", &gl_etc1_backend);
```

### 3.8 API pública (em `gecnd.h`)

```c
/* ── Lifecycle ──────────────────────────────────────────────────────────
 * loop: ponteiro uv_loop_t* — necessário para decoders com use_thread.  */
void gamely_daemon_img_start(void *loop);
void gamely_daemon_img_stop (void);

/* Registro de schema (ex.: "http://", "file://", "db://", "") */
void gamely_daemon_img_register_schema(
    const char           *prefix,
    gamely_img_schema_cb  cb,
    void                 *usr
);

/* Registro de decoder.
 * from: extensão de origem ("png", "jpg", …)
 * to:   formato alvo aceito pelo backend ("rgba", "rgb565", …)
 * use_thread: true → decoder_cb em uv_queue_work
 * Quando from==to o passthrough é automático — não precisa registrar.
 * Último registrado para o mesmo `from` tem maior prioridade. */
void gamely_daemon_img_register_decoder(
    const char            *from,
    const char            *to,
    bool                   use_thread,
    gamely_img_decoder_cb  cb
);

/* Registro de backend GPU — formatos aceitos + callbacks de operação.
 * Último `fmt` registrado tem maior prioridade. */
void gamely_daemon_img_register_backend(
    const char                 *fmt,
    const gamely_img_backend_t *cbs
);

/* Carga assíncrona. ID atribuído imediatamente e retornado.
 * cb/usr podem ser NULL para polling via get_state(). */
typedef void (*gamely_img_load_cb)(int32_t id, void *usr);

int32_t gamely_daemon_img_load_async(
    const char        *url,
    gamely_img_load_cb cb,
    void              *usr
);

/* Consulta de estado (URL→ID é interno ao daemon) */
gamely_img_state_t gamely_daemon_img_get_state(int32_t id);

/* Tick */
void gamely_daemon_img_tick(void);
```

### 3.9 Hooks em `gehook.h` — eliminados para imagem

`native_image_load`, `native_image_draw`, `native_image_unload`, `native_image_unload_all`
e os anteriores `native_image_upload_*` são todos **removidos** do `gehook.h`.  
O backend agora registra draw/unload via `gamely_img_backend_t`.

Permanece apenas:

```c
/* mensure ainda é hook pois as dimensões vêm do cache interno do daemon,
 * não do backend — o daemon as conhece após o decode. */
void native_image_mensure(int32_t id, int16_t *w, int16_t *h);
```

---

## 4. Injeção de schemas em Daemon_Img

Daemon_Img não conhece nenhuma fonte de dados diretamente.  
Cada daemon/driver registra seu handler durante o próprio init.

```c
/* service_fs.c — FS registra-se como resolvedor de paths locais */
gamely_daemon_img_register_schema("file://", fs_img_schema_cb, NULL);
gamely_daemon_img_register_schema("",        fs_img_schema_cb, NULL); /* path sem schema */

/* service_db.c — DB registra-se como resolvedor de URIs db:// */
gamely_daemon_img_register_schema("db://", db_img_schema_cb, NULL);

/* Daemon_WebClient/service_resolver.c — resolvedor HTTP genérico.
 * Usa gamely_daemon_webclient_http() internamente; acumula o response
 * completo e entrega via gamely_img_on_fetch_cb(data, len, hint, usr).
 *
 * hint: Content-Type do response primeiro ("image/png" → "png",
 *       "image/x-etc1" → "etc1", …); fallback para extensão da URL.
 *
 * Hoje: registrado em Daemon_Img para "http://" e "https://".
 * Futuro: pode ser registrado em Daemon_Media para schemas como
 *         "http+libretro://" (ROMs remotas tocadas diretamente). */
gamely_daemon_img_register_schema("http://",  wc_resolver_img_cb, NULL);
gamely_daemon_img_register_schema("https://", wc_resolver_img_cb, NULL);
```

Daemon_Media é **independente** — tem seu próprio mecanismo de registro de players
(`gamely_daemon_media_register_schema`) e não se comunica com Daemon_Img.

---

## 5. Frontend_Api — mudanças em `image.c`

`image.c` vira thin wrapper sobre `Daemon_Img`. O `khash` privado atual é removido.

**Todo load é assíncrono.** `native_image_load()` retorna o ID imediatamente e o daemon
resolve em background (FS, HTTP, DB, decode em worker). Não há `load_async` separado.

O game loop simplesmente tenta usar a imagem a cada frame — `draw`, `mensure` e `exists`
são no-op / retornam zero enquanto a imagem não está READY.

```c
static int lua_native_image_load(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    /* cb=NULL, usr=NULL → modo polling; state consultado separadamente */
    int32_t id = gamely_daemon_img_load_async(url, NULL, NULL);
    gamely_img_state_t state = gamely_daemon_img_get_state(id);
    lua_settop(L, 0);
    lua_pushnumber(L,  id);
    lua_pushinteger(L, (int)state);   /* SEARCHING=0 ou READY=2 se já cacheado */
    return 2;
}

static int lua_native_image_mensure(lua_State *L) {
    /* aceita string (URL) ou integer (ID) */
    int32_t id = ...; /* resolve URL→id se necessário */
    int16_t w = 0, h = 0;
    native_image_mensure(id, &w, &h);   /* hook por referência */
    lua_settop(L, 0);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int lua_native_image_error(lua_State *L) {
    /* aceita string (URL) ou integer (ID) */
    const char *err_msg = ...;
    if (err_msg) lua_pushstring(L, err_msg);
    else         lua_pushnil(L);
    return 1;
}
```

Funções Lua expostas:

| Lua | SEARCHING/DECODING | READY | ERROR |
|-----|--------------------|-------|-------|
| `native_image_load(url)` → id, state | ID + state imediatos (READY se já cacheado) | — | — |
| `native_image_exists(url_or_id)` → bool | `false` | `true` | `false` |
| `native_image_error(url_or_id)` → str\|nil | `nil` | `nil` | `"msg"` |
| `native_image_mensure(url_or_id, &w, &h)` → — | `w=0, h=0` | `w, h` | `w=0, h=0` |
| `native_image_draw(url_or_id, x, y)` → id | **omitido silenciosamente** | desenha | **omitido silenciosamente** |
| `native_image_unload(url_or_id)` → bool | libera | libera | **obrigatório** |
| `native_image_unload_all()` → bool | — limpa tudo — | | |

`native_image_draw()` é sempre seguro — se não estiver READY, simplesmente omite. Game loop continua sem guards.

```lua
-- game loop típico
function draw()
    native_image_draw(bg_id,   0,   0)   -- carregando? omitido
    native_image_draw(hero_id, 10, 10)   -- corrompeu? omitido
    native_image_draw(hud_id,  0,   0)   -- pronto? desenha
end

-- uso do segundo retorno para evitar frame de espera
local id, state = native_image_load("hud.png")
if state == GLY_IMG_READY then
    native_image_draw(id, 0, 0)   -- já estava cacheado, disponível agora
end
```

> `native_image_state()` — em aberto (§9 item 1).

---

## 6. Fluxos de exemplo

### 6.1 Arquivo local — ETC1 preferido

```
native_image_load("textures/hud.png")
 │
 ▼  ID=7 atribuído [state: SEARCHING]
Daemon_Img → schema "" → fs_img_fetch_cb
 │
gamely_daemon_fs_search(
    paths = { exe_cwd, "/app/assets", NULL },
    files = { "textures/hud", NULL },
    exts  = { ".etc1", ".png", NULL },   ← .etc1 tentado primeiro
    GLY_FS_ASYNC | GLY_FS_CALLBACK, on_found_cb, usr)
→ "/app/assets/textures/hud.etc1"
gamely_daemon_fs_read(path, GLY_FS_ASYNC, ..., on_read_cb, usr)  → bytes
 │
 ▼  [state: DECODING]
from="etc1", to="etc1" → passthrough automático (use_thread=false)
     → memdup interno, sem chamar decoder registrado
upload_cb("etc1", id=7, data, len, 512, 512, release_cb)
 │
 ▼  [state: READY]  return 7
```

### 6.2 DB — dois níveis de redirect

```
native_media_src(1, "db://media/url?short=5.1")
 │
Daemon_Media → schema "db://" → db_media_fetch_cb
 │
Daemon_DB resolve("db://media/url?short=5.1")
    → store: tabela media, campo url, where short='5.1'
    → tipo TEXT → "db://blob/data?id=2"

Daemon_Media re-despacha "db://blob/data?id=2"
 │
Daemon_DB resolve("db://blob/data?id=2")
    → tipo BLOB → bytes de vídeo, hint="h264"
 │
playback com bytes brutos
```

### 6.3 DB — imagem de plugin como blob

```
native_image_load("db://blob/data?id=2")
 │
 ▼  ID=8 atribuído [state: SEARCHING]
Daemon_Img → schema "db://" → db_img_fetch_cb
 │
Daemon_DB resolve → BLOB + hint="etc1"
 │
 ▼  [state: DECODING]
from="etc1", to="etc1" → passthrough automático (use_thread=false, registrado assim)
     → upload_cb("etc1", id=8, data, len, 256, 256, release_cb)
 │
 ▼  [state: READY]
```

### 6.4 PNG grande — dois workers paralelos

```lua
local id_a = native_image_load("bg_left.png")
local id_b = native_image_load("bg_right.png")
-- id_a e id_b disponíveis imediatamente; imagens ainda em SEARCHING

-- game loop:
-- frame N:   exists(id_a)=false → no-op
-- frame N+k: exists(id_a)=true  → draw(id_a, ...)
```

```
Daemon_Img agenda dois fetches async (Daemon_FS)
 │
 ├─ bytes bg_left.png prontos   [id_a: DECODING]
 │    decoder["png"]→"rgba" (use_thread=true, registrado assim)
 │    uv_queue_work → worker A: decoder_cb(data, len) → {pixels_A, w, h}
 │
 └─ bytes bg_right.png prontos  [id_b: DECODING]
      decoder["png"]→"rgba" (use_thread=true)
      uv_queue_work → worker B: decoder_cb(data, len) → {pixels_B, w, h}

gamely_daemon_img_tick() — main thread:
 ├─ worker A done → upload_cb("rgba", id_a, pixels_A, …, release_cb) → [READY] → cb(id_a, usr)
 └─ worker B done → upload_cb("rgba", id_b, pixels_B, …, release_cb) → [READY] → cb(id_b, usr)
```

### 6.5 Game loop: draw sempre seguro, erro opcional

```lua
local bg   = native_image_load("background.png")
local hero = native_image_load("hero.png")
local hud  = native_image_load("hud.etc1")

function draw()
    -- sempre seguros: omitidos enquanto carregam ou se falharem
    native_image_draw(bg,   0,   0)
    native_image_draw(hero, 10, 10)
    native_image_draw(hud,  0,   0)

    -- tratamento de erro explícito (opcional, ex.: UI de diagnóstico)
    if native_image_error(hero) then
        log("hero falhou: " .. native_image_error(hero))
        native_image_unload(hero)   -- libera entrada ERROR do cache
        hero = native_image_load("hero_fallback.png")
    end
end
```

---

## 7. Estrutura de diretórios

```
lib/
├── Daemon_FS/
│   ├── service_fs.c        # API pública
│   ├── driver_uv.c         # uv_fs_* async
│   └── driver_std.c        # opendir/fopen fallback
├── Daemon_DB/
│   ├── service_db.c        # store interno, resolve, BLOB vs URL
│   └── driver_sqlite.c     # backend SQLite opcional
├── Daemon_Img/
│   ├── service_img.c       # cache, state machine, dispatch
│   ├── service_atlas.c     # atlas RGBA + Compressed
│   └── driver_spng_uv.c    # PNG + uv_queue_work
├── Daemon_WebClient/
│   └── service_resolver.c  # resolvedor HTTP genérico; hoje registrado em
│                            # Daemon_Img ("http://","https://"); futuro:
│                            # também em Daemon_Media ("http+libretro://", …)
├── Common_Utils/
│   └── http.c              # utilitários de URI: schema, host, path, query params
├── Daemon_Media/
│   └── service_playback.c  # + gamely_daemon_media_register_schema() (futuro)
└── Frontend_Api/
    └── image.c             # thin wrapper sobre Daemon_Img

plugins/
└── libretro/               # (migrado de lib/Frontend_Libretro)
    └── …                   # usa Daemon_FS para scan de ROMs, shaders, assets

include/
└── gecnd.h                 # todas as funções públicas
```

---

## 8. Todas as assinaturas a criar

### 8.1 `gecnd.h` — Daemon_FS

```c
/* ── Modo de busca ──────────────────────────────────────────────────── */
typedef enum {
    GLY_FS_ONE,            /* sync,  primeiro resultado — a=char**, b=NULL      */
    GLY_FS_ONE_CB,         /* sync,  primeiro resultado — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB,         /* sync,  todos os resultados — a=gamely_fs_cb, b=usr */
    GLY_FS_ONE_CB_ASYNC,   /* async, primeiro resultado — a=gamely_fs_cb, b=usr */
    GLY_FS_ALL_CB_ASYNC,   /* async, todos os resultados — a=gamely_fs_cb, b=usr */
} gamely_fs_mode_t;

/* ── Callbacks ─────────────────────────────────────────────────────── */
typedef void (*gamely_fs_cb)     (const char *path, void *usr);
typedef void (*gamely_fs_read_cb)(uint8_t *data, size_t len, void *usr);

/* ── Lifecycle ──────────────────────────────────────────────────────── */
void gamely_daemon_fs_start(void *loop);
void gamely_daemon_fs_stop (void);

/* ── Funções ─────────────────────────────────────────────────────────
 * paths[]: diretórios ou paths completos; suportam glob (/mnt/*/*/roms).
 * files[]: NULL=paths diretos, ["*"]=lista dir, ["name"]=busca arquivo.
 * exts[] : NULL=sem filtro, [".png",".etc1"]=filtra extensão.
 * a/b    : GLY_FS_ONE→(char**,NULL)  |  demais→(gamely_fs_cb, usr).
 * Retorna 0 se encontrou ≥1 resultado (ou async iniciado), -1 se não. */
int  gamely_daemon_fs_search(const char      **paths,
                              const char      **files,
                              const char      **exts,
                              gamely_fs_mode_t  mode,
                              void             *a,
                              void             *b);

/* on_done=NULL → sync (preenche out_data/out_len).
 * on_done!=NULL → async; entregue via tick(). */
int  gamely_daemon_fs_read  (const char        *path,
                              uint8_t          **out_data,
                              size_t            *out_len,
                              gamely_fs_read_cb  on_done,
                              void              *usr);

/* Drena fila de callbacks async — chamado pela main loop. */
void gamely_daemon_fs_tick(void);
```

---

### 8.2 `gecnd.h` — Daemon_DB

```c
/* ── Lifecycle ──────────────────────────────────────────────────────── */
void gamely_daemon_db_start(void);   /* abre app.db, cria tabelas padrão */
void gamely_daemon_db_stop (void);   /* fecha SQLite                      */

/* Insere/atualiza entrada de mídia. Retorna id gerado.
 * url_image pode ser NULL. */
int32_t     gamely_daemon_db_insert_media(const char *name,
                                           const char *short_id,
                                           const char *url,
                                           const char *type,
                                           const char *url_image);
void        gamely_daemon_db_delete_media(const char *short_id);

/* Insere bytes brutos no cache de blobs.
 * hint: "etc1", "png", … (pode ser NULL).
 * Retorna id para usar em "db://blob/data?id=<N>". */
int32_t     gamely_daemon_db_insert_blob(const uint8_t *data,
                                          size_t         len,
                                          const char    *hint);
void        gamely_daemon_db_delete_blob(int32_t id);

/* Chave-valor persistente (substitui ThirdParty_Api/storage.c). */
void        gamely_daemon_db_kv_set(const char *key, const char *value);
const char *gamely_daemon_db_kv_get(const char *key); /* NULL se ausente */
```

---

### 8.3 `gecnd.h` — Daemon_Img

```c
/* ── Lifecycle ──────────────────────────────────────────────────────── */
void gamely_daemon_img_start(void *loop);  /* loop para uv_queue_work */
void gamely_daemon_img_stop (void);

/* ── Enums ──────────────────────────────────────────────────────────── */
typedef enum {
    GLY_IMG_FMT_UNKNOWN  = 0,
    GLY_IMG_FMT_RGBA8888 = 1,
    GLY_IMG_FMT_RGB565   = 2,
    GLY_IMG_FMT_ETC1     = 3,
    GLY_IMG_FMT_ETC2     = 4,
} gamely_img_fmt_t;

typedef enum {
    GLY_IMG_SEARCHING = 0,
    GLY_IMG_DECODING  = 1,
    GLY_IMG_READY     = 2,
    GLY_IMG_ERROR     = 3,
} gamely_img_state_t;

/* ── Decoder ─────────────────────────────────────────────────────────
 * decoder_cb aloca pixels; daemon libera via release_cb após upload.
 * from==to → passthrough automático, não precisa registrar.            */
typedef struct {
    uint8_t *pixels;
    int16_t  w, h;
} gamely_img_decoded_t;

typedef gamely_img_decoded_t (*gamely_img_decoder_cb)(const uint8_t *data,
                                                        size_t         len);

/* ── Schema fetch ────────────────────────────────────────────────────
 * schema_cb dispara o fetch; chama on_done quando bytes chegam.
 * hint: extensão sugerida pelo provider ("etc1", "png", …) ou NULL.
 * data alocado pelo provider; daemon faz free após uso.                */
typedef void (*gamely_img_on_fetch_cb)(const uint8_t *data, size_t len,
                                        const char    *hint, void   *usr);
typedef void (*gamely_img_schema_cb)  (const char           *url,
                                        void                 *schema_usr,
                                        gamely_img_on_fetch_cb on_done,
                                        void                 *on_done_usr);

/* ── Backend ─────────────────────────────────────────────────────────
 * upload_cb: backend escreve handle GPU em *backend_data;
 *            chama release(data) quando GPU terminar (sync ou async PBO).
 * draw_cb / unload_cb: recebem o mesmo backend_data setado no upload.  */
typedef void (*gamely_img_release_cb)(void *ptr);

typedef void (*gamely_img_upload_cb)(int32_t               id,
                                      void                **backend_data,
                                      const uint8_t        *data,
                                      size_t                len,
                                      int16_t               w,
                                      int16_t               h,
                                      gamely_img_release_cb release);

typedef struct {
    gamely_img_upload_cb upload;
    void (*draw)      (int32_t id, void *backend_data, int16_t x, int16_t y);
    void (*unload)    (int32_t id, void *backend_data);
    void (*unload_all)(void);
} gamely_img_backend_t;

/* ── Load callback ───────────────────────────────────────────────────  */
typedef void (*gamely_img_load_cb)(int32_t id, void *usr);

/* ── Registro ────────────────────────────────────────────────────────  */

/* Registra handler de fetch para um prefixo de URL.
 * Exemplos: "http://", "file://", "db://", "" (sem schema = path local). */
void gamely_daemon_img_register_schema (const char            *prefix,
                                         gamely_img_schema_cb   cb,
                                         void                  *usr);

/* Registra decoder from→to. use_thread=true → rodado em uv_queue_work.
 * from==to → passthrough automático, não chamar esta função.
 * Último registrado para mesmo `from` tem maior prioridade.            */
void gamely_daemon_img_register_decoder(const char            *from,
                                         const char            *to,
                                         bool                   use_thread,
                                         gamely_img_decoder_cb  cb);

/* Registra backend GPU para um formato.
 * Último `fmt` registrado tem maior prioridade na escolha de formato.  */
void gamely_daemon_img_register_backend(const char                *fmt,
                                         const gamely_img_backend_t *cbs);

/* ── Carga ───────────────────────────────────────────────────────────
 * ID atribuído imediatamente. cb/usr=NULL → modo polling.              */
int32_t gamely_daemon_img_load_async(const char        *url,
                                      gamely_img_load_cb cb,
                                      void              *usr);

/* ── Consultas ───────────────────────────────────────────────────────  */
int32_t            gamely_daemon_img_get_id     (const char *url); /* -1 se não registrado */
gamely_img_state_t gamely_daemon_img_get_state  (int32_t id);
void               gamely_daemon_img_get_mensure(int32_t id, int16_t *w, int16_t *h);
const char        *gamely_daemon_img_get_error  (int32_t id); /* NULL se sem erro */
bool               gamely_daemon_img_exists     (int32_t id); /* true só se READY */

/* ── Controle ────────────────────────────────────────────────────────  */
bool gamely_daemon_img_unload    (int32_t id);
void gamely_daemon_img_unload_all(void);
void gamely_daemon_img_tick      (void);
```

---

### 8.4 `gecnd.h` — Daemon_Media (fora do escopo atual)

Daemon_Media é independente de Daemon_Img e Daemon_DB.  
Ele registra **players** de mídia contínua (não resolvedores de imagem).  
API a definir quando o escopo for aberto.

```c
/* placeholder — a ser especificado no escopo de Daemon_Media */
// void gamely_daemon_media_register_schema(const char *prefix, …);
```

---

### 8.5 `lib/Common_Utils/http.c` — utilitários de URI

Funções internas (sem exposição em `gecnd.h`) usadas por `service_db.c`,
`service_resolver.c` e qualquer outro código que precise parsear URIs.

```c
/* Extrai schema da URI → "http", "db", "file", "" (sem schema).
 * Retorna tamanho escrito em buf (sem '\0'); buf pode ser NULL para medir. */
size_t gly_uri_schema(const char *uri, char *buf, size_t len);

/* Extrai host/authority → "example.com", "blob" em "db://blob/…". */
size_t gly_uri_host  (const char *uri, char *buf, size_t len);

/* Extrai path → "/image.png", "/data" em "db://blob/data?…". */
size_t gly_uri_path  (const char *uri, char *buf, size_t len);

/* Retorna ponteiro para início da query string (após '?'), ou NULL. */
const char *gly_uri_query(const char *uri);

/* Busca valor de key na query string; retorna bytes escritos ou -1 se ausente. */
int gly_uri_query_get(const char *uri, const char *key, char *buf, size_t len);

/* Itera sobre todos os pares key=value da query string. */
typedef void (*gly_uri_query_cb)(const char *key,   size_t klen,
                                  const char *value, size_t vlen,
                                  void       *usr);
void gly_uri_query_each(const char *uri, gly_uri_query_cb cb, void *usr);

/* Mapeia Content-Type HTTP para hint de formato.
 * "image/png" → "png", "image/x-etc1" → "etc1", desconhecido → NULL. */
const char *gly_http_content_type_hint(const char *content_type);
```

---

### 8.6 `gehook.h` — remoções

Os seguintes hooks de imagem são **eliminados** (substituídos pelo pipeline do Daemon_Img):

```c
/* REMOVIDOS: */
// void native_image_load    (const char *src, int32_t id, bool *success);
// void native_image_draw    (int32_t id, int16_t x, int16_t y);
// void native_image_mensure (int32_t id, int16_t *w, int16_t *h);
// void native_image_unload  (int32_t id, bool *success);
// void native_image_unload_all(bool *success);
```

---

### 8.7 `Frontend_Api/image.c` — funções Lua

| Assinatura Lua | Comportamento | Delega para |
|----------------|---------------|-------------|
| `native_image_load(url)` → `id, state` | ID imediato + state atual | `load_async(url,NULL,NULL)` + `get_state(id)` |
| `native_image_exists(url\|id)` → `bool` | `true` só se READY | `gamely_daemon_img_exists(id)` |
| `native_image_error(url\|id)` → `str\|nil` | msg de erro ou nil | `gamely_daemon_img_get_error(id)` |
| `native_image_mensure(url\|id)` → `w, h` | 0,0 se não READY | `gamely_daemon_img_get_mensure(id,&w,&h)` |
| `native_image_draw(url\|id, x, y)` → `id` | omitido silenciosamente se não READY | backend `draw_cb` via daemon |
| `native_image_unload(url\|id)` → `bool` | obrigatório em ERROR | `gamely_daemon_img_unload(id)` |
| `native_image_unload_all()` → `bool` | descarga tudo | `gamely_daemon_img_unload_all()` |

> `url\|id`: wrapper Lua resolve string→id via `gamely_daemon_img_get_id(url)`
> antes de chamar a função C correspondente.

---

### 8.8 Contagem final

| Módulo | Funções/tipos públicos novos |
|--------|------------------------------|
| Daemon_FS | 2 lifecycle + 1 enum + 2 callbacks + 3 funções = **8** |
| Daemon_DB | 2 lifecycle + 6 funções = **8** |
| Daemon_Img | 2 lifecycle + 14 funções = **16** |
| Common_Utils/http.c | 6 funções + 1 typedef (interno, sem gecnd.h) |
| Daemon_Media | 0 (fora do escopo atual) |
| **Total público** (`gecnd.h`) | **32** |

---

## 9. Decisões fechadas

| Item | Decisão |
|------|---------|
| Probe flag | Removido — `use_thread` é estático no registro do decoder |
| Passthrough `from==to` | Automático no daemon, sem registro explícito |
| ID em ERROR | Sempre válido; `unload()` obrigatório para liberar a entrada |
| `native_image_load()` retorno | `id, state` (dois valores Lua via `get_state`) |
| ETC1 fallback | Backend só registra `"etc1"` se realmente suportar — daemon nunca vê o caso |
| Handle GPU no backend | `void **backend_data` por imagem, gerenciado pelo daemon |
| `SET_VALUE + ASYNC` | Não aceito; `SET_VALUE` é sempre sync; retorno int indica sucesso |
| `files=NULL` vs `["*"]` | `NULL`=paths diretos; `["*"]`=lista diretório; paths suportam glob |
| `native_storage_get` migração | `kv_get` retorna `const char*` síncrono; wrapper Lua faz `lua_pushstring` direto |
| Scanner libretro | Será migrado para `gamely_daemon_fs_search` quando libretro virar plugin |
