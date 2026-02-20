#version 100
precision mediump float;
varying vec2 v_texCoord;
uniform sampler2D tex_rgba;
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
uniform int format;

uniform float u_brightness;
uniform float u_contrast;
uniform float u_saturation;
uniform float u_film_grain;
uniform float u_time;
uniform float u_scratch;
uniform float u_jitter;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  vec2 uv = v_texCoord;
  
  if (u_jitter > 0.1) {
    float jTime = floor(u_time * 15.0);
    uv.x += (rand(vec2(jTime, 0.0)) - 0.5) * 0.003 * u_jitter;
  }

  lowp vec4 color;
  if (format == 1) {
    float y = texture2D(tex_y, uv).r;
    float u = texture2D(tex_u, uv).r - 0.5;
    float v = texture2D(tex_v, uv).r - 0.5;
    // Fast YUV to RGB approx
    color.r = y + 1.402 * v;
    color.g = y - 0.344 * u - 0.714 * v;
    color.b = y + 1.772 * u;
    color.a = 1.0;
  } else {
    color = texture2D(tex_rgba, uv);
  }

  // Optimize color adjustments
  if (abs(u_contrast - 1.0) > 0.01 || abs(u_brightness - 0.5) > 0.01) {
    color.rgb = color.rgb * u_contrast + (u_brightness - 0.5 * u_contrast - 0.5);
  }
  
  if (abs(u_saturation - 1.0) > 0.01) {
    lowp float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(vec3(gray), color.rgb, u_saturation);
  }

  if (u_film_grain > 0.05) {
    float n = rand(uv + u_time * 0.1) * u_film_grain;
    color.rgb += n - u_film_grain * 0.5;
  }

  if (u_scratch > 0.1) {
    float s = sin(uv.x * 200.0 + u_time * 10.0);
    if (s > 0.995) color.rgb += 0.2 * u_scratch;
  }

  gl_FragColor = color;
}
