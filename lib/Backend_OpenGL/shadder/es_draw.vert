#version 100
precision mediump float;

attribute vec2 a_pos;        // GL_SHORT
attribute vec2 a_uv;         // GL_UNSIGNED_SHORT normalized
attribute vec4 a_color;      // GL_UNSIGNED_BYTE normalized
attribute vec4 a_pack;       // GL_UNSIGNED_SHORT_4_4_4_4 normalized
attribute float a_radius;    // GL_UNSIGNED_BYTE normalized

uniform mat4 u_proj;

varying mediump vec2 v_uv;
varying lowp vec4 v_color;
varying lowp vec2 v_local;
varying lowp float v_border;
varying lowp float v_radius;

void main()
{
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);

    v_uv     = a_uv;
    v_color  = a_color;
    v_local  = a_pack.xy * 2.0 - 1.0;
    v_border = a_pack.z;
    v_radius = a_radius;
}
