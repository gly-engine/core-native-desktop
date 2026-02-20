#version 100
precision mediump float;
uniform sampler2D u_texture;
uniform vec4 u_color;
varying vec2 v_texCoord;
uniform vec2 u_texelSize;
uniform float u_aa_blur;

void main() {
    lowp float alpha = texture2D(u_texture, v_texCoord).r;

    if (u_aa_blur > 0.1) {
        // Simplified 3-tap blur instead of 5
        lowp float a2 = texture2D(u_texture, v_texCoord + u_texelSize * u_aa_blur).r;
        lowp float a3 = texture2D(u_texture, v_texCoord - u_texelSize * u_aa_blur).r;
        alpha = (alpha * 2.0 + a2 + a3) * 0.25;
    }

    gl_FragColor = vec4(u_color.rgb, u_color.a * alpha);
}
