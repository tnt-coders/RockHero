$input v_color0, v_world_z

// Floor furniture's vertex color: alpha takes the near fade toward the hit line as well as the
// far-edge fade-in every program shares. Applying both here keeps the distance fades separate
// from any x-taper already carried in the interpolated vertex color.
#include <bgfx_shader.sh>
#include "highway_fade.sh"

void main()
{
    float ramp = highwayNearFade(v_world_z) * highwayFarFade(v_world_z);
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * ramp);
}
