# Design: Daemon_Font

> Status: proposta — em discussão, nada implementado ainda
> Escopo desta rodada: só Backend_OpenGL (SDL2/Raylib ficam de fora, como no Daemon_Img)

---

## Motivação

Hoje texto não tem daemon nenhum por trás. `Frontend_Api/text.c` só expõe
stubs (`GECND_NATIVE_STUB`) ligados por `backend_func:*` à implementação de
cada backend. Só o OpenGL usa fontstash (`Backend_OpenGL/render/text.c`), com
uma única fonte embutida no binário (Roboto, baixada via CMake e virada
header com `xxd -i`). `native_text_font_name` é literalmente no-op nesse
backend hoje.

Objetivo:
1. Dropar fontstash, usar só FreeType + um loader dinâmico de glifos próprio.
2. Fontes carregáveis por URL (arquivo local **ou HTTP**), do mesmo jeito que
   `Daemon_Img` já faz para imagens.
3. Trocar a fonte padrão embutida por
   `https://cdn.jsdelivr.net/npm/@gamely/font-mono-retro/font.ttf`.

---

## Diferença estrutural em relação ao Daemon_Img

Imagem é *decode uma vez → sobe um buffer inteiro*. Fonte é *parseia o
arquivo uma vez, rasteriza glifo por glifo sob demanda* (tamanho de fonte
muda, texto muda). Isso não cabe direto no molde
`gamely_img_backend_t{upload,draw,unload}`. Proposta de split:

```
┌──────────────────────────────────────────────────────────────────┐
│  Daemon_Font  — resolve fontes (URL → ID → face)                 │
│                                                                    │
│  schemas registrados (resolvedores de URL), mesmo padrão de img:  │
│   ""        → service_resolver_fs.c  (path local sem schema)      │
│   "file://" → service_resolver_fs.c  (path local com schema)      │
│   "http://" → Daemon_Web  (download HTTP)                         │
│   "https://"→ Daemon_Web  (download HTTPS)                        │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ bytes crus do arquivo (.ttf/.otf)
                    ┌──────▼──────┐
                    │  driver_    │  FT_New_Memory_Face → "face" opaco
                    │  freetype.c │  + loader dinâmico de glifos (cache/atlas
                    │             │  pack, rasteriza sob demanda)
                    └──────┬──────┘
                           │ glifo já rasterizado (bitmap alpha8 + métricas)
                    ┌──────▼──────┐
                    │ font_backend│  "burro": só sobe bitmap no atlas e
                    │ (OpenGL)    │  desenha quad — GPU-specific apenas
                    └─────────────┘
```

Ponto central: **quem decide "qual glifo rasterizar, onde cabe no atlas, o
que já está em cache" é o driver FreeType**, não o backend gráfico. O backend
só sabe "aqui está um retângulo de bitmap alpha8, sobe pra textura" e "desenha
esse quad". Isso é o oposto do fontstash (que embutia tudo — pack, cache e
render — atrás de uma única API C que já vinha com renderer OpenGL
plugável). A vantagem de separar: se um dia SDL2/Raylib ganharem
`font_backend`, herdam o cache/pack de graça.

O **layout de texto** (percorrer utf-8, pedir glifo a glifo, avançar cursor)
também fica centralizado no daemon — não em cada backend nem na
Frontend_Api — do mesmo jeito que `gamely_daemon_img_draw` já centraliza o
dispatch de imagem.

---

## 1. `lib/Daemon_Font/service_font.c`

Espelha `Daemon_Img/service_img.c`: array fixo de entries (id, url, state,
face opaco), state machine `SEARCHING → DECODING → READY / ERROR`, dispatch
via `gecnd_registry`:

- `font_resolver:<scheme>` — busca bytes (igual `image_resolver:`)
- `font_decoder:<ext>` — `(data,len) -> void*` (face opaco), sem fallback
  multi-formato (TTF/OTF cobrem o caso de uso; imagem precisa de fallback
  porque tem N formatos de pixel, fonte não)
- `font_backend:<atlas-fmt>` — registrado pelo backend gráfico
- alias nome→id (`gamely_daemon_font_set_family` / `get_id_by_family`), usado
  por `native_text_font_family`/`native_text_font_name`

### Máquina de estados (idêntica à de imagem)

```c
typedef enum {
    GLY_FONT_SEARCHING = 0,
    GLY_FONT_DECODING  = 1,
    GLY_FONT_READY     = 2,
    GLY_FONT_ERROR     = 3,
} gamely_font_state_t;
```

### API pública proposta (`gecnd.h`, seção nova "---- Daemon_Font ----")

```c
typedef void *(*gamely_font_decoder_cb)(const uint8_t *data, size_t len); /* -> face opaco, NULL em erro */

typedef void (*gamely_font_on_fetch_cb)(const uint8_t *data, size_t len, const char *hint, void *usr);
typedef void (*gamely_font_schema_cb)(const char *url, void *schema_usr,
                                       gamely_font_on_fetch_cb on_done, void *on_done_usr);

typedef struct {
    int16_t atlas_x, atlas_y, w, h;   /* posição/tamanho no atlas */
    int16_t bearing_x, bearing_y;
    int16_t advance;
} gamely_font_glyph_t;

/* backend gráfico — só sobe bitmap e desenha quad */
typedef struct {
    void (*atlas_upload)(int x, int y, int w, int h, const uint8_t *bitmap /* alpha8 */);
    void (*draw_quad)(int16_t dst_x, int16_t dst_y, int16_t dst_w, int16_t dst_h,
                       float u0, float v0, float u1, float v1, uint32_t rgba);
    void (*unload_all)(void);
} gamely_font_backend_t;

void gamely_daemon_font_start(void *loop);
void gamely_daemon_font_stop (void);

int32_t             gamely_daemon_font_get_id     (const char *url);
int32_t             gamely_daemon_font_load_memory(const void *data, size_t len); /* fonte embutida, sem resolver */
gamely_font_state_t gamely_daemon_font_get_state  (int32_t id);
const char         *gamely_daemon_font_get_error  (int32_t id);
void                gamely_daemon_font_unload_id  (int32_t id);
void                gamely_daemon_font_unload_url (const char *url);
void                gamely_daemon_font_unload_all (void);

void    gamely_daemon_font_set_family     (const char *name, int32_t id);
int32_t gamely_daemon_font_get_id_by_family(const char *name); /* -1 se não achou */

/* layout centralizado, usa o backend/driver registrados */
void gamely_daemon_font_draw_text   (int32_t font_id, uint8_t px_size,
                                      int16_t x, int16_t y, const char *utf8, uint32_t rgba);
void gamely_daemon_font_mensure_text(int32_t font_id, uint8_t px_size,
                                      const char *utf8, int16_t *w, int16_t *h);

void gamely_daemon_font_opengl_register(void); /* backend GL */
```

---

## 2. `lib/Daemon_Font/service_resolver_fs.c`

Versão simplificada de `Daemon_Img/service_resolver_fs.c`: registra
`font_resolver:file$0` e `font_resolver:$s`, lê via
`gamely_daemon_fs_read`/`gamely_daemon_fs_search` (cwd + exe_cwd). **Sem** a
lógica de fallback de formato (png→etc1/tga/jpeg) que só faz sentido pra
imagem — fonte não tem "formato alternativo mais barato".

---

## 3. `lib/Daemon_Font/driver_freetype.c`

- `FT_Library` global, uma vez.
- Registra `font_decoder:ttf`/`font_decoder:otf` (ou uma chave curinga —
  `FT_New_Memory_Face` autodetecta pelo conteúdo).
- Decoder guarda uma cópia dos bytes (FreeType não copia o buffer de
  entrada) e devolve o `FT_Face` como "face opaco".
- **Loader dinâmico de glifos** — a parte "sua", não do FreeType em si: cache
  por `(face, px_size, codepoint) → gamely_font_glyph_t`; shelf-packing
  simples dentro da região de atlas reservada pra fonte; ao faltar um glifo,
  `FT_Set_Pixel_Sizes` + `FT_Load_Char(FT_LOAD_RENDER)`, pega
  `face->glyph->bitmap` (8-bit alpha), decide onde cabe, chama
  `font_backend->atlas_upload(...)`, guarda no cache.
- É chamado pelo `service_font.c` dentro de `draw_text`/`mensure_text` pra
  resolver glifo a glifo (registro extra tipo `font_glyph_provider`).

---

## 4. `lib/Daemon_Web/` — fetch HTTP compartilhado

Hoje `service_img_resolver.c` já implementa fetch HTTP streaming
(`fetch_ctx_t` + `on_status/on_data/on_done/on_error` acumulando bytes via
`gdweb_control_client()->http(...)`). Extraio isso pra uma função
reaproveitável e uso tanto pra imagem quanto pra fonte:

```c
typedef void (*gamely_fetch_done_cb)(const uint8_t *data, size_t len, const char *hint, void *usr);
void gamely_web_fetch(const char *url, const char *hint_override,
                      gamely_fetch_done_cb on_done, void *usr);
```

- `service_img_resolver.c` passa a só derivar o hint da URL e chamar
  `gamely_web_fetch`.
- `service_font_resolver.c` (novo) registra `font_resolver:http$0`/`https$0`
  e chama a mesma função.

---

## 5. `Backend_OpenGL`

### 5.1 `render/font_backend.c` (novo)

Reaproveita o mecanismo que já existe hoje em
`glfons__renderCreate/Update/Draw` (textura compartilhada
`corner_page_index`, região `GE_FONT_ATLAS_SIZE=1024` dentro de
`GE_ATLAS_SIZE=2048`, upload via `glTexSubImage2D` com `GL_ALPHA`, desenho
via `ge_batch_add_vertex_alpha` + `ge_pipeline_flush_primitives`) — só que
implementando a interface enxuta `gamely_font_backend_t` em vez da API do
fontstash. Registra em `gamely_daemon_font_opengl_register()`.

### 5.2 `render/text.c` (reescrito, bem menor)

Sem fontstash. Só guarda seleção local (`current_font_id`, `current_size`):

- `native_text_font_size(size)` → guarda `current_size`.
- `native_text_font_name(name)` → **só aceita nome de família registrado**
  via `native_text_font_family` antes; `get_id_by_family(name)`. Path/URL cru
  não é mais aceito neste backend.
- `native_text_font_default(index)` → `(void)index`; garante a fonte
  embutida carregada (`ensure_default_loaded()`, ver abaixo) e seleciona
  `get_id_by_family("default")`.
- `native_text_print` → `ensure_default_loaded()` se `current_font_id == -1`;
  depois `gamely_daemon_font_draw_text(current_font_id, current_size, x, y, text, cor_atual)`.
- `native_text_mensure` → mesma garantia, depois `gamely_daemon_font_mensure_text(...)`.

`ensure_default_loaded()` é **lazy**, igual ao `ensure_font_loaded()` de hoje
com fontstash: só chama `gamely_daemon_font_load_memory(...)` +
`set_family("default", id)` na primeira vez que `print`/`mensure` rodam sem
nenhuma fonte selecionada, ou quando `native_text_font_default()` é chamado
explicitamente — o que vier primeiro. `Daemon_Font` em si não carrega nada
sozinho no `start()`; quem decide o gatilho é o backend, como hoje.

---

## 6. `Frontend_Api/text.c`

Nova função, backend-agnóstica (estilo `image.c`, via `GECND_NATIVE_DAEMON`
— não passa pelo `backend_func` por-backend):

```c
GECND_NATIVE_DAEMON(native_text_font_family, (const char *name, const char *url, int32_t *out_id));

static void daemon_native_text_font_family(const char *name, const char *url, int32_t *out_id) {
    int32_t id = gamely_daemon_font_get_id(url);
    gamely_daemon_font_set_family(name, id);
    *out_id = id;
}
```

Lua: `native_text_font_family(name, url)`. `url` pode ser path local ou
`http(s)://`. `native_text_font_previous` continua como está (já marcado
`@todo remove on 0.4.X`, fora de escopo).

---

## 7. CMakeLists.txt

- Remove a seção FontStash inteira (`FONTSTASH_DIR/DOWNLOAD` +
  `FetchContent_Populate` condicional a `GECND_USE_GL_EGL OR GECND_USE_GL_GLFW`).
- Substitui a seção "Fonts" (baixa NotoSans + Roboto, gera dois headers via
  `xxd -i`) por uma fonte só: baixa
  `https://cdn.jsdelivr.net/npm/@gamely/font-mono-retro/font.ttf` →
  `vendor/fonts/font-mono-retro/font.ttf` → header
  `include/gecnd/font_mono_retro.h`, mesmo padrão `add_custom_command` + `xxd -i`.
- Novo `cmake/libfreetype.cmake` seguindo o padrão de `cmake/libjpeg.cmake`
  (`ExternalProject_Add`, build estático, `add_library(freetype STATIC
  IMPORTED)`), condicionado à mesma flag que o fontstash tinha
  (`GECND_USE_GL_EGL OR GECND_USE_GL_GLFW`).

---

## 8. Decisões já fechadas

| Item | Decisão |
|------|---------|
| SDL2 / Raylib | Fora de escopo nesta rodada |
| `native_text_font_default(index)` | Vira alias fixo pra família `"default"`; `index` é ignorado (igual hoje) |
| Fetch HTTP | Generalizado: `gamely_web_fetch` compartilhado entre `image_resolver` e `font_resolver` |
| Decode de fonte | Produz um *face* opaco (FT_Face), não pixels; **síncrono** (parsear TTF/OTF é rápido, sem thread pool) |
| Cache/pack de glifos | Vive no driver FreeType (backend-agnóstico), não no backend gráfico |
| Layout de texto (draw/mensure) | Centralizado no daemon, não em cada backend |
| Atlas cheio | Sem eviction nesta rodada — igual ao fontstash hoje, só libera em `unload_all()` |
| `native_text_font_name(arg)` | Só aceita nome de família registrado via `native_text_font_family`; path/URL cru não é mais aceito |
| Fonte padrão (`font-mono-retro`) | Lazy: carregada só quando `print`/`mensure` rodam sem fonte selecionada, ou quando `native_text_font_default()` é chamado — o que vier primeiro. `Daemon_Font.start()` não carrega nada sozinho |
