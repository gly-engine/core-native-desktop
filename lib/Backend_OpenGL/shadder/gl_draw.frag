#version 120

varying vec2 v_uv;
varying vec4 v_color;
varying vec2 v_local;
varying vec2 v_size;
varying vec2 v_sdf;

uniform sampler2D u_atlas;

void main() {
    vec4 tex = texture2D(u_atlas, v_uv);
    float alpha = tex.a * v_color.a;
    
    vec2 halfSize = v_size * 0.5;
    vec2 p = v_local * halfSize;
    vec2 d = abs(p) - halfSize + v_sdf.x;
    float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - v_sdf.x;
    
    float fillMask = clamp(0.5 - dist, 0.0, 1.0);
    float border = v_sdf.y;
    float outlineMask = clamp(0.5 - (abs(dist + border*0.5) - border*0.5), 0.0, 1.0);
    
    float shapeMask = mix(fillMask, outlineMask, step(0.001, border));
    alpha *= mix(1.0, shapeMask, step(0.001, v_size.x));

    gl_FragColor = vec4(tex.rgb * v_color.rgb, alpha);
}
