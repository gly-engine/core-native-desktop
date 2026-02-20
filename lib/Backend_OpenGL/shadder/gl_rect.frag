#version 120
uniform vec4 u_color;
uniform vec4 u_rect;      // x, y, width, height
uniform float u_radius;
uniform float u_thickness;
uniform float u_aa_blur;
uniform int u_mode;      // 0 for filled, 1 for line
varying vec2 v_pos;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 center = u_rect.xy + u_rect.zw / 2.0;
    vec2 size = u_rect.zw;

    float dist = sdRoundedBox(v_pos - center, size / 2.0, u_radius);
    
    float smooth_edge = 1.0 + u_aa_blur;
    float alpha = clamp(0.5 - dist / smooth_edge, 0.0, 1.0);
    
    if (u_mode == 1) { // line
        float alpha_inner = clamp(0.5 - (dist + u_thickness) / smooth_edge, 0.0, 1.0);
        alpha -= alpha_inner;
    }

    if (alpha <= 0.0) {
        discard;
    }

    gl_FragColor = vec4(u_color.rgb, u_color.a * alpha);
}
