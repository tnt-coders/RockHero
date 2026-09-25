$input v_color0, v_texcoord0, v_world_z

// Straight texture sampling modulated by the vertex color (the reference's plain texture
// shader); alpha rides the texture so transparent atlas regions stay transparent. The far-edge
// fade scales all four channels, because this program draws premultiplied texels.
#include <bgfx_shader.sh>
#include "highway_fade.sh"

SAMPLER2D(s_atlas, 0);

void main()
{
    gl_FragColor = texture2D(s_atlas, v_texcoord0) * v_color0 * highwayFarFade(v_world_z);
}
