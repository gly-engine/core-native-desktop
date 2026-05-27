#version 120
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;

varying vec2 v_uv;
varying vec4 v_color;

void main()
{
    // Planes uploaded as GL_ALPHA, so read the .a channel. Y full-res, Cb/Cr
    // half-res sharing the same normalized UV. BT.601 full-range YCbCr->RGB.
    float y = texture2D(tex_y, v_uv).a;
    float u = texture2D(tex_u, v_uv).a - 0.5;
    float v = texture2D(tex_v, v_uv).a - 0.5;
    vec3 c;
    c.r = y + 1.402   * v;
    c.g = y - 0.34414 * u - 0.71414 * v;
    c.b = y + 1.772   * u;
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0) * v_color;
}
