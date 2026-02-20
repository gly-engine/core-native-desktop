#version 120
uniform sampler2D u_texture;
varying vec2 v_texCoord;
uniform vec2 u_texelSize;
uniform float u_aa_blur;

void main() {
    vec4 texel = texture2D(u_texture, v_texCoord);
    
    if (u_aa_blur > 0.1) {
        vec4 t2 = texture2D(u_texture, v_texCoord + u_texelSize * u_aa_blur);
        vec4 t3 = texture2D(u_texture, v_texCoord - u_texelSize * u_aa_blur);
        texel = (texel * 2.0 + t2 + t3) * 0.25;
    }
    
    gl_FragColor = texel;
}
