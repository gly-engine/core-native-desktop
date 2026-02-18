#version 100
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_texCoord;
uniform vec2 u_texelSize;
uniform float u_blur;
uniform float u_weightCenter;
uniform float u_weightNeighbor;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord) * u_weightCenter;
    color += texture2D(u_texture, v_texCoord + vec2(u_texelSize.x, 0.0) * u_blur) * u_weightNeighbor;
    color += texture2D(u_texture, v_texCoord + vec2(-u_texelSize.x, 0.0) * u_blur) * u_weightNeighbor;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, u_texelSize.y) * u_blur) * u_weightNeighbor;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, -u_texelSize.y) * u_blur) * u_weightNeighbor;
    gl_FragColor = color;
}
