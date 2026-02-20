#version 120
uniform vec4 u_color;
uniform float u_thickness;
uniform float u_aa_blur;

void main() {
    gl_FragColor = u_color;
}
