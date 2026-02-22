#version 100
#extension GL_OES_standard_derivatives : enable
precision mediump float;

uniform sampler2D u_tex;
uniform float u_line_width;

varying lowp vec4 v_color;
varying mediump vec2 v_uv;
varying mediump vec2 v_local;
varying mediump float v_mode;
varying mediump float v_radius;
varying mediump float v_u;

mediump float roundedMask(mediump vec2 p, mediump float r)
{
    //mediump vec2 d = abs(p) - (1.0 - r);
    return 1.0;
}

void main()
{
    mediump float isTex = step(0.5, v_u);
    mediump float isBorder = step(v_mode, -1.5);
    mediump float isRound = step(0.001, v_radius);

    mediump vec2 p = v_local;
    mediump float dist = max(abs(p.x), abs(p.y));
    
    mediump float mask = mix(step(dist, 1.0), roundedMask(p, v_radius), isRound);
    
    mediump float borderRel = u_line_width * fwidth(dist) * 0.5;
    
    mediump vec2 pInner = p / max(0.001, 1.0 - borderRel);
    mediump float distInner = max(abs(pInner.x), abs(pInner.y));
    mediump float maskInner = mix(step(distInner, 1.0), roundedMask(pInner, v_radius), isRound);
    
    mediump float finalMask = mix(mask, mask * (1.0 - maskInner), isBorder);

    lowp vec4 texColor = texture2D(u_tex, v_uv);
    lowp vec4 shapeColor = vec4(v_color.rgb, v_color.a * finalMask);

    gl_FragColor = mix(shapeColor, texColor * v_color, isTex);
}
