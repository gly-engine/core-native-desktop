#version 100
precision mediump float;
uniform sampler2D u_texture;
uniform vec4 u_color;
varying vec2 v_texCoord;

void main() {
    float alpha = texture2D(u_texture, v_texCoord).a;
    gl_FragColor = vec4(u_color.rgb, u_color.a * alpha);
}
