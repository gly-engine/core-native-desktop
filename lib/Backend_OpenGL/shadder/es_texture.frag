#version 100
precision mediump float;

uniform sampler2D u_tex;

varying mediump vec2 v_uv;
varying lowp vec4 v_color;

void main()
{
    gl_FragColor = texture2D(u_tex, v_uv) * v_color;
}
