#version 120

varying vec4 v_color;
varying vec2 v_pos;
varying vec2 v_size;
varying float v_radius;
varying float v_mode;

void main()
{
    // Pixel-space SDF calculation
    // v_pos is in pixel offset from center
    // v_size is half-size in pixels
    // v_radius is radius in pixels
    
    vec2 q = abs(v_pos) - v_size + v_radius;
    float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - v_radius;

    // AA in pixel space (assume 1 pixel blur)
    float alpha = clamp(0.5 - dist, 0.0, 1.0);

    if(v_mode > 1.0) // 2.0 is border mode
    {
        // For border mode, assume fixed 2px thickness for now
        float thickness = 2.0;
        float alpha_inner = clamp(0.5 - (dist + thickness), 0.0, 1.0);
        alpha -= alpha_inner;
    }

    if (alpha <= 0.0) discard;
    gl_FragColor = vec4(v_color.rgb, v_color.a * alpha);
}
