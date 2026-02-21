#version 100
precision mediump float;
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;
attribute vec2 a_local;
attribute vec2 a_size;
attribute vec2 a_sdf;

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
