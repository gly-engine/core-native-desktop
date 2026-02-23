#version 120
uniform sampler2D u_tex;

varying vec2 v_uv0;
varying vec2 v_uv1;
varying vec4 v_color;

void main()
{
    gl_FragColor = texture2D(u_tex, v_uv0) * v_color;
}
