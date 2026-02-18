#version 100
precision mediump float;
attribute vec2 a_pos;
attribute vec2 a_texCoord;
uniform mat4 u_projection;
varying vec2 v_texCoord;

void main() {
    gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
