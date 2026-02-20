#version 100
precision mediump float;
uniform sampler2D u_texture;
uniform vec4 u_color;
varying vec2 v_texCoord;
uniform vec2 u_texelSize;
uniform float u_aa_blur;
uniform float u_aa_wC;
uniform float u_aa_wN;

void main() {
    float alpha = texture2D(u_texture, v_texCoord).a;

    if (u_aa_blur > 0.0) {
        float sum = alpha * u_aa_wC;
        sum += texture2D(u_texture, v_texCoord + vec2(u_texelSize.x, 0.0) * u_aa_blur).a * u_aa_wN;
        sum += texture2D(u_texture, v_texCoord - vec2(u_texelSize.x, 0.0) * u_aa_blur).a * u_aa_wN;
        sum += texture2D(u_texture, v_texCoord + vec2(0.0, u_texelSize.y) * u_aa_blur).a * u_aa_wN;
        sum += texture2D(u_texture, v_texCoord - vec2(0.0, u_texelSize.y) * u_aa_blur).a * u_aa_wN;
        alpha = sum;
    }

    gl_FragColor = vec4(u_color.rgb, u_color.a * alpha);
}
