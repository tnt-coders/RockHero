$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

// Glyph-atlas text: position plus atlas coordinates, text color as per-vertex tint so one draw
// carries a whole string of glyphs.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
