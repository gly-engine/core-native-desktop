#version 120

attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;
attribute vec4 a_pack;
attribute float a_radius;

uniform mat4 u_proj;

varying vec2 v_uv;
varying vec4 v_color;
varying vec2 v_local;
varying float v_border;
varying float v_radius;

void main()
{
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);

    v_uv     = a_uv;
    v_color  = a_color;
    v_local  = a_pack.xy * 2.0 - 1.0;
    v_border = a_pack.z;
    v_radius = a_radius;
}
