#version 120
uniform sampler2D u_texture;
uniform vec4 u_color;
varying vec2 v_texCoord;

void main() {
    vec4 texel = texture2D(u_texture, v_texCoord);
    gl_FragColor = vec4(u_color.rgb, u_color.a * texel.a);
}
