$input a_position, a_color0
$output v_color0

// Flat vertex-color surface program: the render surface's first visible draw and the proving
// consumer of the resource-pack loading seam (plan 20 Phase 2).
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
}
