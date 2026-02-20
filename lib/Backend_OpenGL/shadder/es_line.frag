#version 100
precision mediump float;
uniform vec4 u_color;
uniform float u_thickness;
uniform float u_aa_blur;

void main() {
    // For simple lines drawn as quads, we can just use the u_color.
    // Analytical AA for lines requires distance to segment which is complex without more varyings.
    // As a simplification, we just use u_color and u_aa_blur to soften if needed.
    gl_FragColor = u_color;
}
