#version 120
attribute vec3 a_pos;
attribute vec4 a_color;

uniform mat4 u_proj;

varying vec4 v_color;

void main()
{
    gl_Position = u_proj * vec4(a_pos, 1.0);
    v_color = a_color;
}
