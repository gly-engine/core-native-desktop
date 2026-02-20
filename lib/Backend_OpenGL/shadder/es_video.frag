#version 100
precision mediump float;
varying vec2 v_texCoord;
uniform sampler2D tex_rgba;
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
uniform int format; // 0=RGBA, 1=YUV420P

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
  
  if (u_jitter > 0.0) {
    float jTime = floor(u_time * 15.0);
    uv.x += (rand(vec2(jTime, 0.0)) - 0.5) * 0.003 * u_jitter;
    if (rand(vec2(jTime, 1.0)) > 0.98) uv.y += (rand(vec2(jTime, 2.0)) - 0.5) * 0.02 * u_jitter;
  }

  lowp vec4 color;
  if (format == 1) {
    float y = texture2D(tex_y, uv).r;
    float u = texture2D(tex_u, uv).r - 0.5;
    float v = texture2D(tex_v, uv).r - 0.5;
    lowp float r = y + 1.402 * v;
    lowp float g = y - 0.344136 * u - 0.714136 * v;
    lowp float b = y + 1.772 * u;
    color = vec4(r, g, b, 1.0);
  } else {
    color = texture2D(tex_rgba, uv);
  }

  color.rgb = color.rgb * u_contrast + (u_brightness - 0.5 * u_contrast - 0.5);
  
  lowp float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
  color.rgb = mix(vec3(gray), color.rgb, u_saturation);

  if (u_film_grain > 0.0) {
    lowp float n = rand(uv + vec2(u_time * 0.01, u_time * 0.02)) * u_film_grain;
    color.rgb += n - u_film_grain * 0.5;
  }

  if (u_scratch > 0.0) {
    // Vertical scratches
    float s = sin(uv.x * 200.0 + u_time * 10.0);
    if (s > 0.99) {
        lowp float scratch = rand(vec2(floor(uv.x * 200.0 + u_time * 10.0), 0.0));
        if (scratch > 1.0 - u_scratch * 0.5) color.rgb += 0.2 * u_scratch;
    }
    // Random dust/spots
    if (rand(uv + vec2(floor(u_time*10.0))) > 0.999 - (0.01 * u_scratch)) color.rgb -= 0.3 * u_scratch;
    
    // Brightness flicker
    color.rgb *= 1.0 - (rand(vec2(floor(u_time * 15.0))) * 0.1 * u_scratch);
  }

  gl_FragColor = color;
}
