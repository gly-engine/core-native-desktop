#version 120
attribute vec2 a_pos;
uniform mat4 u_projection;
varying vec2 v_pos;

void main() {
    gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);
    v_pos = a_pos;
}
