attribute vec3 a_pos;
attribute vec4 a_uv;
attribute vec4 a_color;

uniform mat4 u_proj;

varying mediump vec2 v_uv0;
varying mediump vec2 v_uv1;
varying lowp vec4 v_color;

void main()
{
    gl_Position = u_proj * vec4(a_pos, 1.0);
    v_uv0 = a_uv.xy;
    v_uv1 = a_uv.zw;
    v_color = a_color;
}