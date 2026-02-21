#version 120
attribute vec2 a_pos;    // loc 0
attribute vec2 a_uv;     // loc 1
attribute vec4 a_color;  // loc 2
attribute vec2 a_local;  // loc 3
attribute vec2 a_sdf;    // loc 4
attribute vec2 a_size;   // loc 5

uniform mat4 u_mvp;

varying vec2 v_uv;
varying vec4 v_color;
varying vec2 v_local;
varying vec2 v_size;
varying vec2 v_sdf;

void main() {
    v_uv = a_uv;
    v_color = a_color;
    v_local = a_local;
    v_size = a_size;
    v_sdf = a_sdf;
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
}
