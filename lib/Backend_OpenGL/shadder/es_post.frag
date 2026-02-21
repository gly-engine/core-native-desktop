#version 100
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_texCoord;
uniform float u_crt;
uniform float u_time;

void main() {
    gl_FragColor = texture2D(u_texture, v_texCoord);
}
