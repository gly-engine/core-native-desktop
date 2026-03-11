# Pipeline Audit — Backend_OpenGL

Foco em flicker, tearing, performance Mali-400 e bugs de memória.
Itens de vídeo (FFmpeg, decode thread, video.c) estão em `VIDEO_AUDIT.md`.

---

## 1. Causa Raiz do Tearing

### 1.1 `eglSwapInterval` não carregado pelo GLAD — vsync nunca ativado

`lib/Backend_OpenGL/window/egl.c:55-56`

```c
PFNEGLSWAPINTERVALPROC eglSwapIntervalPtr = (PFNEGLSWAPINTERVALPROC)eglGetProcAddress("eglSwapInterval");
if (eglSwapIntervalPtr) eglSwapIntervalPtr(egl_display, 0);
```

Dois problemas sobrepostos:

**a)** `eglGetProcAddress("eglSwapInterval")` retorna NULL em alguns drivers Mali quando o GLAD EGL loader não resolve funções core da EGL 1.1 via esse mecanismo. A função existe na `.so` mas não é acessível por `eglGetProcAddress`. A solução é um fallback explícito via `dlopen`/`dlsym` em `libEGL.so`, similar ao que já é feito para `libGLESv2` no `glad_gles2_loader` do mesmo arquivo.

**b)** Mesmo que o ponteiro fosse válido, o argumento passado é `0` — que desabilita vsync. O valor correto para habilitar é `1`.

**Correção sugerida:**

```c
PFNEGLSWAPINTERVALPROC eglSwapIntervalPtr =
    (PFNEGLSWAPINTERVALPROC)eglGetProcAddress("eglSwapInterval");
if (!eglSwapIntervalPtr) {
    void *libEGL = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!libEGL) libEGL = dlopen("libEGL.so", RTLD_LAZY | RTLD_GLOBAL);
    if (libEGL) eglSwapIntervalPtr =
        (PFNEGLSWAPINTERVALPROC)dlsym(libEGL, "eglSwapInterval");
}
if (eglSwapIntervalPtr) eglSwapIntervalPtr(egl_display, 1);
```

---

### 1.2 GLFW sem `glfwSwapInterval`

`lib/Backend_OpenGL/window/glfw.c` — `platform_init()` nunca chama `glfwSwapInterval`.

O default do GLFW para swap interval é `0`. O path desktop também tem tearing por padrão.

**Correção:** adicionar `glfwSwapInterval(1)` após `glfwMakeContextCurrent`.

---

## 2. Bugs Lógicos (OpenGL)

### 2.1 `alpha` computado e descartado em `ge_batch_add_vertex_complex`

`lib/Backend_OpenGL/pipeline/batch.c:76-77`

```c
uint8_t alpha = (color >> 24) & 0xFF;
bool opaque = false;
```

`alpha` é extraído mas nunca usado. `opaque` é hardcoded como `false`. Qualquer forma complexa sempre vai para o batch transparente, independente do alpha real. Isso causa overdraw desnecessário em formas totalmente opacas e pode ordená-las incorretamente em relação a outros elementos opacos na depth sort.

---

## 3. Code Smells

### 3.1 Clear color violeta — intencional para debug de contexto GL

`lib/Backend_OpenGL/pipeline/core.c:194`

```c
glClearColor(1, 0, 1, 1);
```

Cor mantida intencionalmente para confirmar que o contexto OpenGL está ativo. Ao migrar para produção, substituir por `(0, 0, 0, 1)` ou pela cor de fundo da aplicação.

---

### 3.2 Projeção ortográfica recalculada todo frame

`lib/Backend_OpenGL/pipeline/core.c:204`

```c
mat4_ortho(s->projection, 0, (float)s->window_width, (float)s->window_height, 0,
           -(float)GE_MAX_LAYERS, (float)GE_MAX_LAYERS);
```

A matriz só muda em resize. Calculá-la em `ge_pipeline_start()` — que executa todo frame — é desperdício de divisões float. Deveria ser calculada apenas em `ge_pipelineeglSwapIntervalPtr_init` e `ge_pipeline_resize`.

---

### 3.3 Uniform de projeção enviado N vezes por frame sem mudança

`lib/Backend_OpenGL/pipeline/core.c:245`

```c
glUniformMatrix4fv(p->loc_proj, 1, GL_FALSE, s->projection);
```

Dentro de `flush_batch`, chamada até 5 vezes por frame. A mesma matriz imutável é enviada ao driver 5 vezes por frame, uma por flush, mesmo sem resize.

---

### 3.4 `glBlendFunc` enviado todo frame

`lib/Backend_OpenGL/pipeline/core.c:199`

```c
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
```

A blend function nunca muda. Enviá-la em `ge_pipeline_start` todo frame é um driver call gratuito. Pode ser movida para `ge_pipeline_init`.

---

### 3.5 Stride do batch implícito e duplicado

`lib/Backend_OpenGL/pipeline/core.c:127-132` — stride passado para `init_batch`.
`lib/Backend_OpenGL/pipeline/core.c:257-282` — stride re-derivado dentro de `flush_batch` pelo tipo enum.

O struct `GEBatch` não armazena o stride. O buffer é alocado com o stride correto em `init_batch`, mas em `flush_batch` o stride é recalculado independentemente. Se um novo tipo de vértice for adicionado e apenas um dos dois lugares for atualizado, o resultado é corrupção silenciosa de memória.

---

## 4. Performance Mali-400

### 4.1 Estado GL redundante em batches consecutivos do mesmo tipo

`lib/Backend_OpenGL/pipeline/core.c:247-253` dentro de `flush_batch`:

```c
if (transparent) {
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
} else {
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}
```

A ordem de flush em `ge_pipeline_flush_primitives` é:

1. `flush_batch(SIMPLE, opaco)` → desabilita blend, depth write ON
2. `flush_batch(ATLAS, opaco)` → **desabilita blend de novo** (redundante), depth write ON (redundante)
3. `flush_batch(SIMPLE, transparente)` → habilita blend, depth write OFF
4. `flush_batch(ATLAS, transparente)` → **habilita blend de novo** (redundante)
5. `flush_batch(COMPLEX, transparente)` → **habilita blend de novo** (redundante)

3 chamadas redundantes a `glEnable`/`glDisable`/`glDepthMask` por frame. No Mali-400, cada mudança de estado tem custo de sincronização do tile renderer. Rastrear o estado ativo e pular chamadas redundantes elimina essas sincronizações.

---

### 4.2 Buffer orphaning com tamanho máximo independente do batch real

`lib/Backend_OpenGL/pipeline/core.c:293`

```c
glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * stride, NULL, GL_STREAM_DRAW);
glBufferSubData(GL_ARRAY_BUFFER, 0, b->count * stride, b->buffer);
```

O orphan sempre declara `MAX_VERTICES * stride` bytes (~250KB por VBO), mesmo que `b->count` seja 10 vértices. O driver reserva memória de vídeo pelo tamanho declarado em `glBufferData`. No Mali-400 com bandwidth limitada, declarar 250KB para transferir 160 bytes força alocação desnecessária no bus.

O orphan com `NULL` é a técnica correta para evitar stalls — mas o tamanho deveria ser `b->count * stride` em vez de `MAX_VERTICES * stride`.

---

### 4.3 Troca de página do atlas dispara flush de todos os 5 batches

`lib/Backend_OpenGL/pipeline/batch.c:12-14`

```c
if (b->count >= GE_MAX_VERTICES || (b->count > 0 && b->page_index != page_index)) {
    ge_pipeline_flush_primitives();
```

Quando o batch atlas precisa trocar de página de textura, `ge_pipeline_flush_primitives()` é chamado — flushando **todos** os 5 batches mesmo que nenhum deles estivesse cheio. Serializa toda a pipeline por conta de uma mudança de página num único batch.

---

## Sumário por Prioridade

| # | Arquivo | Local | Tipo | Impacto |
|---|---------|-------|------|---------|
| 1 | `egl.c` | :55-56 | Bug crítico | GLAD não carrega eglSwapInterval + valor 0 → tearing |
| 2 | `glfw.c` | `platform_init` | Bug crítico | Vsync nunca habilitado no desktop |
| 3 | `batch.c` | :77 | Bug lógico | Complex sempre transparente, alpha ignorado |
| 4 | `core.c` | :245 | Performance | Uniform de projeção enviado 5x por frame |
| 5 | `core.c` | :247-253 | Performance | 3 state changes GL redundantes por frame |
| 6 | `core.c` | :293 | Performance | Buffer orphan com tamanho máximo sempre |
| 7 | `batch.c` | :13 | Performance | Page switch flushea todos os 5 batches |
| 8 | `core.c` | :199,204 | Code smell | BlendFunc e projeção calculados todo frame |
| 9 | `core.c` | :257-282 | Manutenção | Stride implícito, duplicado em dois lugares |
| 10 | `core.c` | :194 | Anotação | Clear color violeta — remover em produção |
