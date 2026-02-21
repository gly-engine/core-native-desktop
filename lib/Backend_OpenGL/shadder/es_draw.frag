#version 100
precision mediump float;
varying vec2 v_texCoord;
varying vec4 v_color;
uniform sampler2D u_texture;

void main() {
    float mask = texture2D(u_texture, v_texCoord).r;
    gl_FragColor = vec4(v_color.rgb, v_color.a * mask);
}
