#version 100
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_texCoord;
uniform vec2 u_texelSize;
uniform float u_aa_blur;
uniform float u_aa_wC;
uniform float u_aa_wN;
uniform float u_crt;
uniform float u_time;

vec2 curve(vec2 uv) {
    uv = (uv - 0.5) * 2.0;
    uv *= 1.1;	
    uv.x *= 1.0 + pow((abs(uv.y) / 5.0), 2.0);
    uv.y *= 1.0 + pow((abs(uv.x) / 4.0), 2.0);
    uv  = (uv / 2.0) + 0.5;
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

    vec4 color;
    if (u_crt > 0.0) {
        // RGB shift
        float shift = 0.001 * u_crt;
        color.r = texture2D(u_texture, uv + vec2(shift, 0.0)).r;
        color.g = texture2D(u_texture, uv).g;
        color.b = texture2D(u_texture, uv - vec2(shift, 0.0)).b;
        color.a = 1.0;

        // Scanlines
        float scanline = sin(uv.y * 800.0) * 0.04 * u_crt;
        color.rgb -= scanline;

        // Vignette
        float vignette = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
        vignette = clamp(pow(16.0 * vignette, 0.1 * u_crt), 0.0, 1.0);
        color.rgb *= vignette;
    } else {
        color = texture2D(u_texture, uv);
    }
    
    if (u_aa_blur > 0.0) {
        vec4 sum = color * u_aa_wC;
        sum += texture2D(u_texture, uv + vec2(u_texelSize.x, 0.0) * u_aa_blur) * u_aa_wN;
        sum += texture2D(u_texture, uv - vec2(u_texelSize.x, 0.0) * u_aa_blur) * u_aa_wN;
        sum += texture2D(u_texture, uv + vec2(0.0, u_texelSize.y) * u_aa_blur) * u_aa_wN;
        sum += texture2D(u_texture, uv - vec2(0.0, u_texelSize.y) * u_aa_blur) * u_aa_wN;
        color = sum;
    }

    gl_FragColor = color;
}
