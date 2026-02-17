#version 100
precision mediump float;
varying vec2 v_texCoord;
uniform sampler2D tex_rgba;
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
uniform int format; // 0=RGBA, 1=YUV420P

void main() {
  if (format == 1) {
    float y = texture2D(tex_y, v_texCoord).r;
    float u = texture2D(tex_u, v_texCoord).r - 0.5;
    float v = texture2D(tex_v, v_texCoord).r - 0.5;
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    gl_FragColor = vec4(r, g, b, 1.0);
  } else {
    gl_FragColor = texture2D(tex_rgba, v_texCoord);
  }
}
