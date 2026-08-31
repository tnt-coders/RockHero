$input a_position, a_color0
$output v_color0, v_texcoord0

// Distance-faded vertex color. World Z is passed through so the fragment shader applies the
// distance ramp after interpolation; doing it here would multiply that ramp into any x-taper
// already in the vertex alpha and let the triangle split bend the fade.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = vec2(a_position.z, 0.0);
}
