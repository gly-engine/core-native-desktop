# Video Audit — Stream_AVlib + Backend_OpenGL/pipeline/video.c

Itens específicos do pipeline de vídeo (FFmpeg, decode thread, upload de textura).

---

## 1. Memory Leak

### 1.1 Double `strdup` — leak garantido por chamada

`lib/Stream_AVlib/api.c:30`

```c
g_background_video = stream_create(strdup(url));
```

`lib/Stream_AVlib/media.c:151`

```c
s->url = strdup(url);
```

`api.c` cria uma cópia da URL com `strdup` e passa para `stream_create`. Dentro de `stream_create`, **outra** cópia é criada e armazenada em `s->url`. A primeira cópia (criada em `api.c`) nunca é armazenada nem liberada — leak confirmado em toda chamada a `native_media_source`.

O comentário `@bug strdup generate a memory leak?` no código já reconhece o problema.

**Correção:** remover o `strdup` em `api.c:30` e passar `url` diretamente, já que `stream_create` já faz sua própria cópia.

---

## 2. Dead Code

### 2.1 `native_draw_background_video` é inteiramente código morto

`lib/Backend_OpenGL/pipeline/video.c:64`

```c
void native_draw_background_video(void) {
    return; /** @todo: enable */
    // ... ~60 linhas nunca executadas
```

O decode thread FFmpeg roda, consome CPU e memória, faz buffer swaps e atualiza texturas — mas o frame nunca chega à tela porque a função de draw retorna imediatamente. Todo o trabalho do pipeline de vídeo é descartado.

---

### 2.2 Uniforms de vídeo declarados mas nunca enviados

`include/geopengl.h:116-120` — `loc_scratch`, `loc_jitter`, `loc_crt` etc. declarados em `GEProgram`.
`lib/Backend_OpenGL/pipeline/video.c:106` — usa `p->loc_scratch`, `p->loc_jitter`.

Como `native_draw_background_video` retorna cedo (2.1), nenhum desses uniforms é enviado ao shader. Os campos existem, são resolvidos no link do shader mas nunca ativados em runtime.

---

## 3. Performance

### 3.1 `glPixelStorei` set/reset por frame de vídeo

`lib/Backend_OpenGL/pipeline/video.c:44` e `:60`

```c
glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
// ... upload das texturas YUV ...
glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
```

Dois driver calls por frame de vídeo para estado que não muda entre frames. O alinhamento de 1 byte pode ser definido uma vez durante a alocação das texturas (`need_realloc == true`) e mantido enquanto as texturas existirem. O restore para 4 pode ser feito apenas se outro subsistema depender disso.

---

### 3.2 `usleep` no decode thread — spin implícito quando frame está no tempo

`lib/Stream_AVlib/media.c:122`

```c
if (delay > 0.001) usleep((useconds_t)(delay * 1e6));
```

`usleep` é deprecated em POSIX.1-2008. Mais importante: quando `delay <= 0.001s` (frame atrasado ou exatamente no tempo), o loop não dorme e gira livre até o próximo `av_read_frame`. No embedded, esse spin compete com o thread principal de renderização por cache e bandwidth de memória.

Substituir por `nanosleep` ou por um mecanismo de condição que o thread principal sinalize ao consumir o frame.

---

### 3.3 Textura de vídeo sempre aloca 3 handles mesmo para formatos não-YUV

`lib/Backend_OpenGL/pipeline/video.c:12`

```c
glGenTextures(3, s->video_tex);
```

Para formatos RGB565 e RGBA, apenas `video_tex[0]` é usado. Os handles `video_tex[1]` e `video_tex[2]` são alocados no driver mas nunca recebem `glTexImage2D`. São handles válidos ocupando entradas na tabela de texturas do driver sem propósito.

---

## Sumário

| # | Arquivo | Local | Tipo | Impacto |
|---|---------|-------|------|---------|
| 1 | `api.c` | :30 | Memory leak | Double strdup, primeira cópia perdida por chamada |
| 2 | `video.c` | :64 | Dead code | Pipeline de vídeo desabilitada por `return` prematuro |
| 3 | `video.c` | uniforms | Dead code | Uniforms declarados nunca enviados por causa de (2) |
| 4 | `video.c` | :44,60 | Performance | `glPixelStorei` set/reset por frame |
| 5 | `media.c` | :122 | Performance | `usleep` / spin loop quando frame está no tempo |
| 6 | `video.c` | :12 | Performance | 3 texture handles alocados para formatos de 1 plano |
