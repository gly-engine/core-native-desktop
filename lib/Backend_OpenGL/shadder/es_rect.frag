#version 100
precision mediump float;
uniform lowp vec4 u_color;
uniform vec4 u_rect;      // x, y, width, height
uniform float u_radius;
uniform float u_thickness;
uniform float u_aa_blur;
uniform int u_mode;      // 0 for filled, 1 for line
varying vec2 v_pos;

void main() {
    if (u_radius <= 0.0 && u_mode == 0 && u_aa_blur <= 0.0) {
        gl_FragColor = u_color;
        return;
    }

    vec2 center = u_rect.xy + u_rect.zw * 0.5;
    vec2 size = u_rect.zw;
    vec2 p = v_pos - center;
    vec2 b = size * 0.5;

    // Optimized rounded box SDF
    vec2 q = abs(p) - b + u_radius;
    float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - u_radius;
    
    float smooth_edge = 1.0 + u_aa_blur;
    lowp float alpha = clamp(0.5 - dist / smooth_edge, 0.0, 1.0);
    
    if (u_mode == 1) { // line
        lowp float alpha_inner = clamp(0.5 - (dist + u_thickness) / smooth_edge, 0.0, 1.0);
        alpha -= alpha_inner;
    }

    gl_FragColor = vec4(u_color.rgb, u_color.a * alpha);
}
