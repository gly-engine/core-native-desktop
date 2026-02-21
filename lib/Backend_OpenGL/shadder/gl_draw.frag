#version 120

varying vec2 v_pos;
varying vec2 v_uv;
varying vec4 v_color;
varying vec4 v_rect;
varying vec3 v_data;

uniform sampler2D u_atlas;

float roundedBox(vec2 p, vec2 size, float r) {
    vec2 b = size * 0.5;
    vec2 d = abs(p) - (b - vec2(r));
    return length(max(d, 0.0)) - r;
}

void main() {
    vec4 tex = texture2D(u_atlas, v_uv);
    
    float alpha = tex.a * v_color.a;
    
    if (v_rect.z > 0.0) {
        vec2 center = v_rect.xy + v_rect.zw * 0.5;
        vec2 p = v_pos - center;
        float dist = roundedBox(p, v_rect.zw, v_data.x);
        float aa = 1.0;
        
        float fillMask = 1.0 - smoothstep(0.0, aa, dist);
        float borderDist = abs(dist) - v_data.y;
        float outlineMask = 1.0 - smoothstep(0.0, aa, borderDist);
        
        float shapeMask = mix(fillMask, outlineMask, step(0.5, v_data.z));
        alpha *= shapeMask;
    }

    gl_FragColor = vec4(tex.rgb * v_color.rgb, alpha);
}
