#version 100
precision mediump float;
attribute vec4 a_pos;
attribute vec2 a_local;
attribute lowp vec4 a_color;
attribute vec2 a_size;
attribute lowp float a_radius;

uniform mat4 u_proj;

varying lowp vec4 v_color;
varying vec2 v_pos;
varying vec2 v_size;
varying lowp float v_radius;

void main()
{
    gl_Position = u_proj * vec4(a_pos.xyz, 1.0);
    v_color = a_color;
    v_pos = a_local;
    v_size = a_size;
    v_radius = a_radius;
}