$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

// Hand-window light: the vertex carries the fragment's signed distances inside the window's two
// edges in a_texcoord0 (x = distance inside the low edge, y = distance inside the high edge).
// Both are linear in world X within a slice, so interpolating them is exact; the fragment stage
// turns their minimum into a soft-edged brightness mask. Deliberately NO hit-line distance fade
// (user rule 2026-07-23): the light is board coloring, constant along the whole board — the
// reference's near-fade belongs to event furniture (beat bars, rails), and fading it made the
// current window read dimmer than upcoming ones.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
