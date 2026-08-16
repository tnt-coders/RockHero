$input a_position, a_color0, a_texcoord0, a_texcoord1
$output v_color0, v_texcoord0, v_texcoord1

// Accent glow: passes the subject-local offsets (world units from the subject's center) through
// texcoord0 for the fragment stage's exact distance evaluation, and the subject's own shape
// through texcoord1, so one program lights a fretted head, an open string and a chord box frame
// without a branch per subject in the renderer.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
}
