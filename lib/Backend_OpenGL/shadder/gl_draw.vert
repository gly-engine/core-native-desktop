#version 120
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;
attribute vec4 a_rect;
attribute vec3 a_data;

uniform mat4 u_mvp;

varying vec2 v_pos;
varying vec2 v_uv;
varying vec4 v_color;
varying vec4 v_rect;
varying vec3 v_data;

void main() {
    v_pos = a_pos;
    v_uv = a_uv;
    v_color = a_color;
    v_rect = a_rect;
    v_data = a_data;
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
}
