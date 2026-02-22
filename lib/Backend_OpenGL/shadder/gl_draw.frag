#version 120

uniform sampler2D u_tex;
uniform float u_line_width;

varying vec4 v_color;
varying vec2 v_uv;
varying vec2 v_local;
varying float v_mode;
varying float v_radius;
varying float v_u;

float roundedMask(vec2 p, float r)
{
    vec2 d = abs(p) - (1.0 - r);
    return 1.0 - step(r, length(max(d, 0.0)));
}

void main()
{
    float isTex = step(0.5, v_u);
    float isBorder = step(v_mode, -1.5);
    float isRound = step(0.001, v_radius);

    vec2 p = v_local;
    float dist = max(abs(p.x), abs(p.y));
    
    // Máscara principal (corpo do shape)
    float mask = mix(step(dist, 1.0), roundedMask(p, v_radius), isRound);
    
    // Cálculo da borda constante em pixels
    // fwidth nos dá quanto o v_local muda por pixel na tela.
    // Multiplicamos pelo u_line_width (pixels) e dividimos por 2 (pois v_local é -1 a 1, range 2.0)
    float borderRel = u_line_width * fwidth(dist) * 0.5;
    
    vec2 pInner = p / max(0.001, 1.0 - borderRel);
    float distInner = max(abs(pInner.x), abs(pInner.y));
    float maskInner = mix(step(distInner, 1.0), roundedMask(pInner, v_radius), isRound);
    
    float finalMask = mix(mask, mask * (1.0 - maskInner), isBorder);

    vec4 texColor = texture2D(u_tex, v_uv);
    vec4 shapeColor = vec4(v_color.rgb, v_color.a * finalMask);

    gl_FragColor = mix(shapeColor, texColor * v_color, isTex);
}
