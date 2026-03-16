# Backend_OpenGL Refactor

## Motivação

- **Z-fighting**: o uso de `int16_t z` com `GL_DEPTH_TEST + GL_LEQUAL` causa artefatos em geometria coplanar. Profundidade 2D não agrega valor ao pipeline.
- **Batches opacos/transparentes separados**: a separação por opacidade exige flush duplo e destrói a ordem de desenho. Qualquer composição que alterna entre tipos resulta em artefatos.
- **GE_PROG_SIMPLE obsoleto**: o shader simple é redundante. Formas sólidas podem usar o shader de textura com `white_uv` da atlas — mesma lógica, menos um programa GL.
- **Desperdício de bytes**: `z` e `w` em toda a geometria custam 4 bytes por vértice sem benefício funcional após remoção do z-depth.

---

## Novo Modelo de Batch

### Princípio

Um único batch ativo acumula vértices em ordem de desenho. O flush só ocorre quando:

1. O programa GL muda (ex.: de `TEXTURE` para `COMPLEX`)
2. A página de atlas muda dentro do programa `TEXTURE`
3. O buffer atinge `GE_MAX_VERTICES`

Isso garante que a ordem de chamada das funções de render determina a ordem de rasterização — sem reordenação por opacidade ou profundidade.

### Batch como union

O batch é uma estrutura única com union interna — não há dois buffers simultâneos. Os campos `tex` e `complex` são aliases do mesmo bloco de memória alocado com o tamanho do maior formato:

```c
typedef struct {
    union {
        GETexVertex     *tex;
        GEComplexVertex *complex;
        void            *raw;
    };
    int count;
    int page_index;
} GEBatch;
```

Alocação única: `malloc(GE_MAX_VERTICES * sizeof(GEComplexVertex))`.

### Estado ativo

```c
GEBatch batch;       // union — só uma fila existe por vez
int     active_prog; // -1 = vazio
int     active_page; // página de atlas ativa (TEXTURE only)
```

### Fluxo de add_vertex

```
add_vertex(prog, page, ...)
    if prog != active_prog || (prog == TEXTURE && page != active_page) || count >= MAX
        flush_primitives()          // flush apenas o ativo, não-op se vazio
        active_prog = prog
        active_page = page
    append vertex to batch.tex[] ou batch.complex[]
```

### flush_primitives

Flush somente do batch atualmente ativo. Se `active_prog == -1` (vazio), não emite nenhuma chamada GL:

```c
void ge_pipeline_flush_primitives(void) {
    if (active_prog >= 0) flush_batch();
    active_prog = -1;
    active_page = -1;
}
```

---

## Novos Layouts de Vértice

### GETexVertex — 12 bytes (era GEAtlasVertex: 16 bytes, GESimpleShapeVertex: 12 bytes)

```c
typedef struct __attribute__((packed)) {
    int16_t  x, y;       // 4
    uint8_t  r, g, b, a; // 4
    uint16_t u, v;       // 4
} GETexVertex;           // total: 12 bytes
```

Formas sólidas passam `white_uv` nos campos `u, v`. O shader multiplica `texture2D(u_tex, uv) * a_color` — resultado idêntico ao shader simple atual.

O parâmetro `bool opaque` permanece nas funções de adição de vértice para preservar a API e indicação semântica, mas não influencia o roteamento do batch.

### GEComplexVertex — 20 bytes (era GEDShapeComplexVertex: 24 bytes)

```c
typedef struct __attribute__((packed)) {
    int16_t x, y;        // 4
    int16_t px, py;      // 4
    int16_t hw, hh;      // 4
    int16_t radius;      // 2
    int16_t _pad;        // 2  (alinhamento 4-byte)
    uint8_t r, g, b, a;  // 4
} GEComplexVertex;       // total: 20 bytes
```

Remove `z`, `w` e o `pad` semântico após radius (substituído por pad de alinhamento).

---

## Enum de Programas

```c
typedef enum {
    GE_PROG_TEXTURE,   // unifica SIMPLE + ATLAS — vertex: GETexVertex
    GE_PROG_COMPLEX,   // formas com SDF/rounding — vertex: GEComplexVertex
    GE_PROG_VIDEO,     // YUV420P / RGBA — vertex dedicado
    GE_PROG_COUNT
} GEProgramType;
```

`GE_PROG_SIMPLE` é removido. Código que usava `ge_batch_add_vertex_shape()` passa a usar `ge_batch_add_vertex_tex()` com `white_uv`.

---

## Mudanças no GLBackendState

```c
// Remover:
GEBatch opaque_batches[GE_PROG_COUNT];
GEBatch transparent_batches[GE_PROG_COUNT];
int16_t current_z;
int     active_opaque_page_index;
int     active_transparent_page_index;
GLuint  vbo_simple;
GLuint  vbo_atlas;

// Adicionar:
GEBatch batch;       // union — único buffer para tex e complex
int     active_prog; // -1 = nenhum batch ativo
int     active_page;
GLuint  vbo;         // VBO único, tamanho = MAX_VERTICES * sizeof(GEComplexVertex)

// Projeção: near/far = -1/1 (z fixo em 0 por todos os vértices)
```

---

## Mudanças no Pipeline

### ge_pipeline_start

```c
// Remover:
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glDepthMask(GL_TRUE);
glClear(... | GL_DEPTH_BUFFER_BIT);

// Manter sempre ativo:
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

// Reset de batch:
tex_batch.count     = 0;
complex_batch.count = 0;
active_prog         = -1;
active_page         = -1;
```

### flush_batch / ge_pipeline_flush_primitives

`flush_batch` usa `active_prog` para escolher stride e attrib layout. VBO único:

```c
static void flush_batch(void) {
    if (batch.count == 0) return;
    glUseProgram(programs[active_prog].id);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // setup attribs conforme active_prog
    glBufferData(...); glBufferSubData(...);
    glDrawArrays(GL_TRIANGLES, ...);
    batch.count = 0;
}

void ge_pipeline_flush_primitives(void) {
    if (active_prog >= 0) flush_batch();
    active_prog = -1;
    active_page = -1;
}
```

Sem separação opaco/transparente. Sem loops sobre múltiplos batches. Sem depthMask.

---

## Shaders

### Remoção

- `es_shape_simple.vert` / `es_shape_simples.frag`
- `gl_shape_simple.vert` / `gl_shape_simple.frag`

### Texture shader

O shader de textura já implementa `color * texture2D(u_tex, uv)`. Com `white_uv`, a amostragem retorna `vec4(1.0)` e o resultado é a cor do vértice — exatamente o comportamento do simple shader.

O atlas `page 0` mantém o pixel branco em `white_uv[2]` gerado dentro da região do glifo de semicírculo.

### Vertex attribute layout (TEXTURE)

```
location 0: a_pos   — vec2 short  (x, y)
location 1: a_color — vec4 ubyte normalized
location 2: a_uv    — vec2 ushort normalized
```

### Vertex attribute layout (COMPLEX)

```
location 0: a_pos    — vec2 short  (x, y)
location 1: a_color  — vec4 ubyte normalized
location 2: a_local  — vec2 short  (px, py)
location 3: a_size   — vec2 short  (hw, hh)
location 4: a_radius — float short (radius)
```

---

## Arquivos Afetados

| Arquivo | Operação |
|---|---|
| `include/geopengl.h` | `GETexVertex` (12B), `GEComplexVertex` (20B), `GEBatch` com union, estado com `batch`/`vbo` únicos |
| `pipeline/core.c` | VBO único, `flush_batch` usa `active_prog`, `flush_primitives` só flushea o ativo |
| `pipeline/batch.c` | Union `batch.tex[]` / `batch.complex[]`; sem `add_vertex_shape` |
| `pipeline/shaders.c` | Simple shaders removidos; `a_radius` em location 4; `a_mode` removido |
| `render/draw.c` | `add_vertex_shape` → `add_vertex_tex` com `white_uv`; `current_z` removido |
| `render/image.c` | `current_z` removido |
| `render/text.c` | `current_z` removido |
| `window/glfw.c`, `egl.c` | near/far = -1/1 |
| `shadder/` | `*_shape_simple.*` obsoletos (não referenciados) |

---

## Economia de Memória

Com `GE_MAX_VERTICES = 8190`:

| | Antes | Depois |
|---|---|---|
| VBOs GPU | 3 buffers (~425 KB) | 1 buffer (164 KB) |
| Buffers CPU | 3 heaps (~425 KB) | 1 heap (164 KB) |
| **Total** | **~850 KB** | **~328 KB** |

Redução de ~61% — buffer CPU único alocado no tamanho do maior formato (`GEComplexVertex`).

---

## Z-Fighting Residual — Causa e Correção

### Causa

O depth buffer ainda **existe** mesmo sem `GL_DEPTH_TEST` explícito no pipeline. GLFW aloca por padrão 24 bits de depth buffer; EGL aloca conforme o `EGL_DEPTH_SIZE` da config. Qualquer caminho GL externo ao pipeline 2D (video path, FBO, driver de GLES no `eglSwapBuffers`, estado não resetado entre frames) pode deixar `GL_DEPTH_TEST` habilitado. Como todos os vértices 2D resultam em `z_clip = 0`, o depth test lê e compara o mesmo valor para cada primitive, produzindo z-fighting determinístico em qualquer geometria sobreposta.

### Correções

**Fix 1 — Guardar estado no início do frame (defensivo, baixo custo):**

Adicionar em `ge_pipeline_start`:
```c
glDisable(GL_DEPTH_TEST);
glDepthMask(GL_FALSE);
```
Garante que mesmo que um caminho externo (video shader, FBO, driver) habilite depth test, o pipeline 2D sempre começa com ele desativado. `GL_DEPTH_MASK = GL_FALSE` impede escrita acidental no depth buffer.

**Fix 2 — Eliminar o depth buffer na criação da janela (definitivo):**

Em `window/glfw.c`, antes de `glfwCreateWindow`:
```c
glfwWindowHint(GLFW_DEPTH_BITS, 0);
glfwWindowHint(GLFW_STENCIL_BITS, 0);
```

Em `window/egl.c`, nos atributos de `eglChooseConfig`:
```c
EGL_DEPTH_SIZE,   0,
EGL_STENCIL_SIZE, 0,
```

Sem depth buffer alocado, qualquer `glEnable(GL_DEPTH_TEST)` se torna um no-op e o z-fighting se torna fisicamente impossível. Adicionalmente libera `width × height × 2–4 bytes` de memória GPU — relevante no Mali-400 com bandwidth restrito.

### Ordem de implementação

1. Fix 1 em `ge_pipeline_start` — resolve imediatamente sem alterar window backends
2. Fix 2 nos backends de janela — remove o depth buffer permanentemente

---

## Invariantes Mantidos

- `bool opaque` permanece como parâmetro nas funções de adição de vértice
- Posições em `int16_t` — sem float em posição de vértice
- UVs em `uint16_t` normalized
- Cores em `uint8_t` — sem float em cor de vértice
- Batch de no mínimo `GE_MAX_VERTICES` por flush
- Atlas multi-página com alocação linear
- Video path (YUV420P / RGBA) inalterado
- Post-processing inalterado
