$input v_color0, v_texcoord0

// Straight texture sampling modulated by the vertex color (the reference's plain texture
// shader); alpha rides the texture so transparent atlas regions stay transparent.
#include <bgfx_shader.sh>

SAMPLER2D(s_atlas, 0);

void main()
{
    gl_FragColor = texture2D(s_atlas, v_texcoord0) * v_color0;
}
