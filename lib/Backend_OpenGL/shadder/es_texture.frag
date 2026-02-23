#version 100
precision mediump float;

uniform sampler2D u_tex;

varying mediump vec2 v_uv0;
varying mediump vec2 v_uv1;
varying lowp vec4 v_color;

void main()
{
    gl_FragColor = texture2D(u_tex, v_uv0) * v_color;
}
