#version 100
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_texCoord;
uniform float u_crt;
uniform float u_time;

void main() {
    if (u_crt <= 0.0) {
        gl_FragColor = texture2D(u_texture, v_texCoord);
        return;
    }

    vec2 uv = v_texCoord;
    // Simple barrel distortion approximation
    vec2 centered_uv = uv - 0.5;
    float dist = dot(centered_uv, centered_uv);
    uv = uv + centered_uv * dist * 0.15 * u_crt;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    lowp vec4 color;
    // RGB shift (2 lookups is enough for a good effect)
    float shift = 0.0015 * u_crt;
    color.r = texture2D(u_texture, uv + vec2(shift, 0.0)).r;
    color.g = texture2D(u_texture, uv).g;
    color.b = texture2D(u_texture, uv - vec2(shift, 0.0)).b;
    color.a = 1.0;

    // Fast scanlines
    lowp float scanline = (0.95 + 0.05 * sin(uv.y * 600.0)) * u_crt + (1.0 - u_crt);
    color.rgb *= scanline;

    // Fast vignette (no pow)
    lowp float vignette = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y) * 16.0;
    vignette = mix(1.0, vignette, 0.2 * u_crt);
    color.rgb *= vignette;
    
    gl_FragColor = color;
}
