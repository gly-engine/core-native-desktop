#version 120
varying vec2 v_texCoord;
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
uniform float u_brightness;
uniform float u_contrast;
uniform float u_saturation;
uniform float u_film_grain;
uniform float u_time;
uniform float u_scratch;
uniform float u_jitter;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texCoord;

    float jTime = floor(u_time * 15.0);
    uv.x += (rand(vec2(jTime, 0.0)) - 0.5) * 0.003 * u_jitter;

    float y = texture2D(tex_y, uv).a;
    float u = texture2D(tex_u, uv).a - 0.5;
    float v = texture2D(tex_v, uv).a - 0.5;
    vec3 color;
    color.r = y + 1.402   * v;
    color.g = y - 0.34414 * u - 0.71414 * v;
    color.b = y + 1.772   * u;
    color = clamp(color, 0.0, 1.0);

    color = color * u_contrast + (u_brightness - 0.5 * u_contrast - 0.5);
    color = clamp(color, 0.0, 1.0);

    float gray = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(gray), color, u_saturation);

    float n = rand(uv + u_time * 0.1) * u_film_grain;
    color += n - u_film_grain * 0.5;

    color += step(0.995, sin(uv.x * 200.0 + u_time * 10.0)) * 0.2 * u_scratch;

    gl_FragColor = vec4(color, 1.0);
}
