#version 100
precision mediump float;

varying vec2 v_uv;
varying vec4 v_color;
varying vec2 v_local;
varying vec2 v_size;
varying vec2 v_sdf;

uniform sampler2D u_atlas;

void main() {
    // lowp for colors and textures is much faster on Mali-400
    lowp vec4 tex = texture2D(u_atlas, v_uv);
    lowp vec4 color = v_color;
    lowp float alpha = tex.a * color.a;
    
    // For simple textures (size.x == 0), skip complex SDF
    if (v_size.x > 0.5) {
        vec2 halfSize = v_size * 0.5;
        vec2 p = v_local * halfSize;
        float radius = (v_sdf.x / 255.0) * max(v_size.x, v_size.y);
        vec2 d = abs(p) - halfSize + radius;
        float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
        
        lowp float fillMask = clamp(0.5 - dist, 0.0, 1.0);
        lowp float border = v_sdf.y;
        lowp float outlineMask = clamp(0.5 - (abs(dist + border*0.5) - border*0.5), 0.0, 1.0);
        
        lowp float shapeMask = mix(fillMask, outlineMask, step(0.001, border));
        alpha *= shapeMask;
    }

    gl_FragColor = vec4(tex.rgb * color.rgb, alpha);
}
