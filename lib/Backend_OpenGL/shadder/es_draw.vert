#version 100
precision mediump float;
attribute vec2 a_pos;       // loc 0
attribute vec2 a_texCoord;  // loc 1
attribute vec4 a_color;     // loc 2
attribute vec4 a_rect;      // loc 3
attribute vec4 a_params;    // loc 4
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
