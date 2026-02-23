#version 120
attribute vec3 a_pos;
attribute vec2 a_local;
attribute vec4 a_color;
attribute float a_radius;
attribute float a_mode;

uniform mat4 u_proj;

varying vec4 v_color;
varying vec2 v_pos;
varying float v_radius;
varying float v_mode;

void main()
{
    gl_Position = u_proj * vec4(a_pos, 1.0);
    v_color = a_color;
    v_pos = a_local;
    v_radius = a_radius;
    v_mode = a_mode;
}
