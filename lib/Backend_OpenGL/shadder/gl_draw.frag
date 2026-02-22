#version 120

varying vec2 v_uv;
varying vec4 v_color;
varying vec2 v_local;
varying float v_border;
varying float v_radius;

uniform sampler2D u_tex;

void main()
{
    float mask = 1.0;

    if (v_radius > 0.001)
    {
        float r = v_radius;
        vec2 p = abs(v_local);
        float corner = max(p.x, p.y);

        mask = clamp((r - corner) * 100.0, 0.0, 1.0);

        if (v_border > 0.001)
        {
            float inner = r - v_border;
            float innerMask = clamp((inner - corner) * 100.0, 0.0, 1.0);
            mask -= innerMask;
        }
    }

    vec4 tex = texture2D(u_tex, v_uv);
    vec4 color = tex * v_color;

    color.a *= mask;

    gl_FragColor = color;
}
