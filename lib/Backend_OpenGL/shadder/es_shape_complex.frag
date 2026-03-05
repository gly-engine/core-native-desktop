#version 100
precision mediump float;

varying lowp vec4 v_color;
varying vec2 v_pos;
varying vec2 v_size;
varying lowp float v_radius;

void main()
{
    vec2 q = abs(v_pos) - v_size + v_radius;
    float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - v_radius;

    float alpha = clamp(0.5 - dist, 0.0, 1.0);

    // Outline mode (fixed 2px thickness)
    float thickness = 2.0;
    float alpha_inner = clamp(0.5 - (dist + thickness), 0.0, 1.0);
    alpha -= alpha_inner;

    if (alpha <= 0.0) discard;
    gl_FragColor = vec4(v_color.rgb, v_color.a * alpha);
}