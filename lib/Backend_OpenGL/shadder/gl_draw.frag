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
    
    if (v_size.x > 0.0) {
        vec2 halfSize = v_size * 0.5;
        vec2 p = v_local * halfSize;
        float radius = (v_sdf.x / 255.0) * max(v_size.x, v_size.y);
        vec2 d = abs(p) - halfSize + radius;
        float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
        
        float fillMask = clamp(0.5 - dist, 0.0, 1.0);
        float border = v_sdf.y;
        float outlineMask = clamp(0.5 - (abs(dist + border*0.5) - border*0.5), 0.0, 1.0);
        
        float shapeMask = mix(fillMask, outlineMask, step(0.001, border));
        alpha *= shapeMask;
    }

    gl_FragColor = vec4(tex.rgb * v_color.rgb, alpha);
}
