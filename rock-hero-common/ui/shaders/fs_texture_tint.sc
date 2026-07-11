$input v_color0, v_texcoord0

// The reference channel scheme (one atlas serves every string color): texture R multiplies the
// tint, G adds white highlight, B is the alpha mask.
#include <bgfx_shader.sh>

SAMPLER2D(s_atlas, 0);

void main()
{
    vec4 texel = texture2D(s_atlas, v_texcoord0);
    vec3 rgb = (texel.r * v_color0.rgb) + vec3_splat(texel.g);
    gl_FragColor = vec4(rgb, texel.b * v_color0.a);
}
