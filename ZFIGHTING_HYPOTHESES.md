# Hipóteses de Z-Fighting Residual

Com depth buffer removido, `GL_DEPTH_TEST` explicitamente desabilitado e shaders usando
`vec4(a_pos, 0.0, 1.0)`, o depth buffer está definitivamente fora de cena. O artefato
visualmente descrito como "z-fighting" é causado por outra coisa. As hipóteses estão
ordenadas por probabilidade.

---

## H1 — Diagonal de triangulação causa double-blend no Mali-400

Cada quad é emitido como dois triângulos que compartilham a diagonal TL→BR:

```
triângulo A: (x, y),     (x, y+h), (x+w, y+h)
triângulo B: (x, y),     (x+w, y+h), (x+w, y)
```

A diagonal compartilhada (x,y)→(x+w,y+h) deve ser coberta por exatamente um triângulo
pela regra top-left. Em implementações Mali-400 conhecidas, pixels cujo centro cai
precisamente sobre a diagonal podem ser rasterizados por ambos os triângulos dependendo
da direção do edge e da precisão do rasterizador de ponto fixo interno. Com blending
ativo e cor semi-transparente, essa faixa diagonal fica com alpha dobrado — produz
uma linha diagonal visível que cruza o rect, muito similar ao z-fighting clássico.

**Verificação:** trocar a triangulação de todos os quads para usar a diagonal oposta
TL→TR→BL + TR→BR→BL e observar se o artefato diagonal muda de posição.

---

## H2 — Projeção float imprecisa em dimensões não-potência-de-2

`mat4_ortho` calcula `mat[0] = 2.0f / width`. Se `width` não for potência de 2
(ex: 1280, 1920, 1366), essa divisão produz um float com erro de arredondamento.

Exemplo com width=1280:
```
2.0f / 1280.0f = 0.00156249994...  (não representável exatamente em IEEE 754)
```

Dois quads adjacentes com borda compartilhada em `x=640`:
- Quad A (right edge): `clip_x = 0.00156249994 * 640 - 1.0 = 0.99999996... - 1.0 ≈ -3.7e-8`
- Quad B (left edge): mesmo cálculo, mesmo resultado

Isso é consistente — ambos usam o mesmo float. Mas a conversão desse clip coordinate
para coordenada de janela (fixed-point interno do Mali-400, tipicamente 4-8 bits de
subpixel) pode arredondar o edge para pixels diferentes nos dois triângulos dos quads
opostos. O resultado é uma seam de 1 pixel entre quads que deveriam estar justapostos.

**Verificação:** adicionar `+ 0.375` a todas as coordenadas de vértice no vertex shader
(o hack clássico para pixel-perfect 2D no OpenGL ES).

---

## H3 — Vértices inteiros caem em bordas de pixel, não centros

Em OpenGL, o centro do pixel `(i, j)` está em `(i+0.5, j+0.5)`. Coordenadas inteiras
como `x=100` correspondem à **borda** entre pixels 99 e 100 — não ao centro de nenhum.

A regra top-left do rasterizador define qual triângulo "ganha" os pixels da borda.
No Mali-400, essa regra é implementada em hardware com precisão limitada. Para edges
que passam exatamente em `x=integer`, o pixel cujo centro está em `x=integer+0.5`
pode ser classificado como FORA do triângulo (gap de 1px) ou DENTRO de ambos
(double-blend) dependendo da direção e inclinação do edge.

Esse fenômeno afeta especialmente:
- Bordas verticais/horizontais entre rects adjacentes
- A linha y=y+r onde as faixas de preenchimento e os cantos arredondados se encontram

**Verificação:** adicionar `0.5` (ou `0.375` para subpixel bias do Mali) às coordenadas
`x` e `y` no vertex shader para mover os vértices dos pixel edges para os pixel centers.

---

## H4 — `glBufferData(NULL, count*stride)` com tamanho variável não orpha corretamente

O padrão de orphaning no `flush_batch` é:
```c
glBufferData(GL_ARRAY_BUFFER, s->batch.count * stride, NULL, GL_STREAM_DRAW);
glBufferSubData(GL_ARRAY_BUFFER, 0, s->batch.count * stride, s->batch.raw);
```

O VBO foi originalmente alocado com `GE_MAX_VERTICES * sizeof(GEComplexVertex)`.
A cada flush o `glBufferData` é chamado com um tamanho diferente (`count * stride`).

Em alguns drivers Mali-400, chamar `glBufferData` com tamanho diferente do atual
não realiza o orphaning esperado — ao invés disso, o driver pode realizar uma
sincronização bloqueante ou reutilizar o buffer antigo. Se o GPU ainda estiver
consumindo dados do buffer anterior (frame anterior), os novos dados escritos via
`glBufferSubData` podem corromper a leitura em andamento, produzindo vértices com
campos misturados entre frames. Um vértice com `x, y` do frame atual mas cor ou UV
do frame anterior produziria exatamente o artefato visual de "conteúdo de dois frames
competindo na tela".

**Verificação:** substituir `glBufferData(NULL, count*stride)` por
`glBufferData(NULL, GE_MAX_VERTICES * sizeof(GEComplexVertex))` — sempre orpha com
o tamanho máximo fixo, nunca variável.

---

## H5 — `GETexVertex` com stride=12: o `a_color` lê bytes de r,g,b,a mas no shader `v_color.a` pode vir de posição errada

`glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 12, offset=4)` lê 4 bytes
a partir do byte 4 do vértice. O layout em memória é:
```
byte 4: r
byte 5: g
byte 6: b
byte 7: a
```
O GLSL recebe `a_color = vec4(r/255, g/255, b/255, a/255)`.
`gl_FragColor = texture * v_color` onde `texture.a` é multiplicado por `v_color.a`.

O blend usa `GL_SRC_ALPHA` que lê `gl_FragColor.a = texture.a * v_color.a`.
Com `white_uv`, `texture.a = 1.0`, então `gl_FragColor.a = v_color.a = a/255`.

O byte `a` do vértice vem de:
```c
uint8_t *c = (uint8_t*)&color;
vert->a = c[3];
```
onde `color = s->current_color.u32 = __builtin_bswap32(input_color)`.

Se `input_color` está em formato `0xRRGGBBAA` (RGBA big-endian), após bswap32:
u32 = `0xAABBGGRR`. Bytes little-endian: `c[0]=RR, c[1]=GG, c[2]=BB, c[3]=AA`.
`vert->a = c[3] = AA` ✓ correto.

Se `input_color` está em formato `0xAARRGGBB` (ARGB), após bswap32:
u32 = `0xBBGGRRAA`. `c[3] = AA` — ainda correto casualmente.

Se `input_color` está em formato `0xRRGGBBAA` já em little-endian (os bytes já estão
na ordem certa para o hardware), o bswap32 INVERTE a ordem: `AA` vai para `c[0]` e
`RR` vai para `c[3]`. O `vert->a = c[3] = RR` — o componente Red está sendo usado
como alpha. Geometrias com componente Red≈0 seriam invisíveis, e componentes Red≈255
seriam opacos, produzindo transparency incorreta para a maioria das cores.

**Verificação:** logar o valor bruto de `s->current_color.u32` para uma cor
conhecida (ex: branco opaco 0xFFFFFFFF) e verificar se `c[3] == 0xFF`.

---

## H6 — `GE_MAX_CHUNK = 1020` quebra primitivas no ponto errado

O loop de draw divide o batch em chunks de 1020 vértices:
```c
glDrawArrays(GL_TRIANGLES, offset, chunk);
```

1020 = 170 × 6, múltiplo de 6 ✓. Não parte triângulos individuais.

Mas: os quads são emitidos em grupos de 6 vértices que representam um rect ou elemento.
Se um elemento ocupa os vértices 1016–1021 (cruza o chunk boundary), o chunk 1
desenha vértices 0–1019 (apenas os 4 primeiros do elemento), e o chunk 2 desenha
os 2 restantes. Esses 4+2 vértices não formam triângulos válidos isoladamente:
o chunk 1 teria triângulo incompleto (4 vértices = 1 triângulo válido + 1 fragmento),
e o chunk 2 teria 2 vértices (nenhum triângulo). O elemento seria parcialmente
renderizado ou omitido.

Se `GE_MAX_CHUNK` não for múltiplo de 6, isso ocorre sistematicamente. `1020 % 6 = 0` ✓.
Mas `GE_MAX_VERTICES = 8190 = 1365 × 6` e `8190 / 1020 = 8.02...` — o último
chunk tem `8190 - 8 * 1020 = 30` vértices, ainda múltiplo de 6. OK matematicamente.

Porém: o batch acumula vértices de múltiplos elementos. Se um elemento de 6 vértices
começa no vértice 1019 (o chunk termina em 1020), o elemento 1019+0..1019+5 cruza
chunks. Chunk 1 inclui vértices 1019 (só 1 vértice do elemento), chunk 2 inclui
os 5 restantes. `glDrawArrays(TRIANGLES, 0, 1020)` com 1020 vértices = 340 triângulos.
O elemento entre 1019-1024 contribui: triângulo (1018, 1019, 1020) — mas 1020 não
está neste draw call. Triângulo incompleto = vértice (1018, 1019, ??) desenhado com
vértice ?? fora do call. GLES2 `glDrawArrays` com count=1020 lê exatamente 1020
vértices. Os 2 triângulos que cruzam a fronteira são desenhados com vértices do
próximo elemento que casualmente está no buffer. Isso produz triângulos erráticos
visíveis como flashes/z-fighting.

**Verificação crítica:** verificar se `(GE_MAX_CHUNK % 6 == 0)` é suficiente ou se
é necessário garantir que cada elemento (6 vértices) não cruze o chunk boundary.
A solução é garantir que o flush aconteça antes de o buffer atingir
`GE_MAX_VERTICES - 5` para preservar espaço para o próximo elemento completo.

---

## Resumo por Probabilidade

| Hipótese | Causa | Probabilidade |
|---|---|---|
| H1 | Diagonal do quad double-blend no Mali-400 | **Alta** |
| H3 | Vértices inteiros em bordas de pixel | **Alta** |
| H6 | Chunk boundary parte elemento de 6 vértices | **Alta** |
| H2 | Projeção float imprecisa em res não-pot-de-2 | Média |
| H4 | `glBufferData` tamanho variável não orpha | Média |
| H5 | `__builtin_bswap32` inverte alpha incorretamente | Baixa |
