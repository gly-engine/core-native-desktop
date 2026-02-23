#version 100
precision mediump float;

uniform float u_thickness;
uniform float u_aa_blur;
uniform vec2 u_size;

varying lowp vec4 v_color;
varying vec2 v_pos;
varying lowp float v_radius;
varying lowp float v_mode;

void main()
{
    vec2 p = v_pos; 
    vec2 half_size = vec2(1.0, 1.0);
    
    vec2 q = abs(p) - half_size + v_radius;
    float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - v_radius;

    float smooth_edge = u_aa_blur / min(u_size.x, u_size.y);
    float alpha = clamp(0.5 - dist / smooth_edge, 0.0, 1.0);

    if(v_mode == 2.0)
    {
        float thickness = u_thickness / min(u_size.x, u_size.y);
        float alpha_inner = clamp(0.5 - (dist + thickness) / smooth_edge, 0.0, 1.0);
        alpha -= alpha_inner;
    }

    gl_FragColor = vec4(v_color.rgb, v_color.a * alpha);
}