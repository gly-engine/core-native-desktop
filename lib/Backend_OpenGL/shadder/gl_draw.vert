#version 120
attribute vec2 a_pos;
attribute vec2 a_texCoord;
attribute vec4 a_color;
attribute vec4 a_rect;
attribute vec4 a_params;
uniform mat4 u_projection;
varying vec2 v_pos;
varying vec2 v_texCoord;
varying vec4 v_color;
varying vec4 v_rect;
varying vec4 v_params;

void main() {
    gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);
    v_pos = a_pos;
    v_texCoord = a_texCoord;
    v_color = a_color;
    v_rect = a_rect;
    v_params = a_params;
}
