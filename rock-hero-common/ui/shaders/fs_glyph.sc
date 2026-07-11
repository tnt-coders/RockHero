$input v_color0, v_texcoord0

// Glyph-atlas text: the atlas alpha masks the tint color (glyphs rasterize white on
// transparent, so alpha alone carries the shape).
#include <bgfx_shader.sh>

SAMPLER2D(s_atlas, 0);

void main()
{
    float mask = texture2D(s_atlas, v_texcoord0).a;
    gl_FragColor = vec4(v_color0.rgb, mask * v_color0.a);
}
