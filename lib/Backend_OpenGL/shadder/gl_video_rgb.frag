#version 120
varying vec2 v_texCoord;
uniform sampler2D tex_rgba;

void main() {
    gl_FragColor = texture2D(tex_rgba, v_texCoord);
}
