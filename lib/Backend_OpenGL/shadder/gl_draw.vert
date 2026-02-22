#version 120

attribute vec2 a_pos;
attribute vec4 a_color;
attribute vec4 a_param;

uniform mat4 u_proj;

varying vec4 v_color;
varying vec2 v_uv;
varying vec2 v_local;
varying float v_mode;
varying float v_radius;
varying float v_u;

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
