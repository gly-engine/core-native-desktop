#version 120
attribute vec2 a_pos;
attribute vec2 a_texCoord;
uniform mat4 u_projection;
uniform float u_rotation;
uniform vec2 u_center;
varying vec2 v_texCoord;

void main() {
    vec2 pos = a_pos;
    if (u_rotation != 0.0) {
        float s = sin(u_rotation);
        float c = cos(u_rotation);
        vec2 p = a_pos - u_center;
        pos.x = p.x * c - p.y * s + u_center.x;
        pos.y = p.x * s + p.y * c + u_center.y;
    }
    gl_Position = u_projection * vec4(pos, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
