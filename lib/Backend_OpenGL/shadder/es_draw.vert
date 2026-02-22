#version 100
precision mediump float;

attribute vec2 a_pos;
attribute vec4 a_color;
attribute vec4 a_param;

uniform mat4 u_proj;

varying lowp vec4 v_color;
varying mediump vec2 v_uv;
varying mediump vec2 v_local;
varying mediump float v_mode;
varying mediump float v_radius;
varying mediump float v_u;

void main()
{
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
    v_u = a_param.x;
    v_uv = a_param.xy / 32767.0;
    v_mode = a_param.x;
    v_radius = a_param.y / 32767.0;
    v_local = a_param.zw / 32767.0;
}
