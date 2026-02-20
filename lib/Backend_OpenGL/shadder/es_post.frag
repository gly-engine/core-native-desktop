#version 100
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_texCoord;
uniform float u_crt;
uniform float u_time;

vec2 curve(vec2 uv) {
    uv = (uv - 0.5) * 2.0;
    uv *= 1.1;
    float d2y = uv.y * 0.2;
    float d2x = uv.x * 0.25;
    uv.x *= 1.0 + (d2y * d2y);
    uv.y *= 1.0 + (d2x * d2x);
    uv  = (uv * 0.5) + 0.5;
    uv =  uv * 0.92 + 0.04;
    return uv;
}

void main() {
    vec2 uv = v_texCoord;
    if (u_crt > 0.0) {
        uv = mix(v_texCoord, curve(v_texCoord), u_crt);
    }

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    lowp vec4 color;
    if (u_crt > 0.0) {
        // RGB shift
        float shift = 0.001 * u_crt;
        color.r = texture2D(u_texture, uv + vec2(shift, 0.0)).r;
        color.g = texture2D(u_texture, uv).g;
        color.b = texture2D(u_texture, uv - vec2(shift, 0.0)).b;
        color.a = 1.0;

        // Scanlines
        lowp float scanline = sin(uv.y * 800.0) * 0.04 * u_crt;
        color.rgb -= scanline;

        // Vignette
        float vignette = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
        vignette = clamp(pow(16.0 * vignette, 0.1 * u_crt), 0.0, 1.0);
        color.rgb *= vignette;
    } else {
        color = texture2D(u_texture, uv);
    }
    
    gl_FragColor = color;
}
