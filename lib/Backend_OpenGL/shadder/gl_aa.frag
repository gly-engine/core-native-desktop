#version 120
uniform sampler2D u_texture;
varying vec2 v_texCoord;
uniform vec2 u_texelSize;
uniform float u_aa_blur;
uniform float u_aa_wC;
uniform float u_aa_wN;
uniform float u_brightness;
uniform float u_contrast;
uniform float u_saturation;
uniform float u_film_grain;
uniform float u_sharpen;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    
    if (u_sharpen > 0.0) {
        vec4 sharp = color * 5.0;
        sharp -= texture2D(u_texture, v_texCoord + vec2(u_texelSize.x, 0.0)) * u_sharpen;
        sharp -= texture2D(u_texture, v_texCoord - vec2(u_texelSize.x, 0.0)) * u_sharpen;
        sharp -= texture2D(u_texture, v_texCoord + vec2(0.0, u_texelSize.y)) * u_sharpen;
        sharp -= texture2D(u_texture, v_texCoord - vec2(0.0, u_texelSize.y)) * u_sharpen;
        color = mix(color, sharp, u_sharpen);
    }

    if (u_aa_blur > 0.0) {
        vec4 sum = color * u_aa_wC;
        sum += texture2D(u_texture, v_texCoord + vec2(u_texelSize.x, 0.0) * u_aa_blur) * u_aa_wN;
        sum += texture2D(u_texture, v_texCoord - vec2(u_texelSize.x, 0.0) * u_aa_blur) * u_aa_wN;
        sum += texture2D(u_texture, v_texCoord + vec2(0.0, u_texelSize.y) * u_aa_blur) * u_aa_wN;
        sum += texture2D(u_texture, v_texCoord - vec2(0.0, u_texelSize.y) * u_aa_blur) * u_aa_wN;
        color = sum;
    }

    color.rgb = (color.rgb - 0.5) * u_contrast + 0.5 + (u_brightness - 1.0);
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(vec3(gray), color.rgb, u_saturation);

    if (u_film_grain > 0.0) {
        float n = rand(v_texCoord) * u_film_grain;
        color.rgb += n - u_film_grain * 0.5;
    }

    gl_FragColor = color;
}
