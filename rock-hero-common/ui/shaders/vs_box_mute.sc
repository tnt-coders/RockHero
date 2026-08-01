$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

// Repeat-box mute mark: passes the box-local offsets (world units from the box center) through
// texcoord for the fragment stage's exact distance evaluation, plus a modulate color.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
