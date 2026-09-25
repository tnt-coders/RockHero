$input v_color0, v_world_z

// Flat vertex-color surface program: passes the interpolated vertex color through, faded in at
// the far edge.
#include <bgfx_shader.sh>
#include "highway_fade.sh"

void main()
{
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * highwayFarFade(v_world_z));
}
