attribute vec4 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;

uniform mat4 u_proj;

varying mediump vec2 v_uv;
varying lowp vec4 v_color;

void main()
{
    gl_Position = u_proj * vec4(a_pos.xyz, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
