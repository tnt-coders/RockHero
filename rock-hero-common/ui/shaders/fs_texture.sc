$input v_color0, v_texcoord0, v_world_z

// Straight texture sampling modulated by the vertex color (the reference's plain texture
// shader); alpha rides the texture so transparent atlas regions stay transparent. The texels are
// premultiplied, so the vertex color is premultiplied here too: its alpha then scales the whole
// sample as an opacity, where a straight color's alpha would only thin the blend and leave the
// color at full strength. The far-edge fade scales all four channels for the same reason.
#include <bgfx_shader.sh>
#include "highway_fade.sh"

SAMPLER2D(s_atlas, 0);

void main()
{
    vec4 tint = vec4(v_color0.rgb * v_color0.a, v_color0.a);
    gl_FragColor = texture2D(s_atlas, v_texcoord0) * tint * highwayFarFade(v_world_z);
}
