$input a_position, a_color0
$output v_color0

// Distance-faded vertex color for beat bars: alpha ramps linearly on world Z between
// u_fade_params.x (fully transparent) and u_fade_params.y (fully opaque). The ramp is linear in
// Z, so evaluating it per vertex and interpolating is exact.
#include <bgfx_shader.sh>

uniform vec4 u_fade_params;

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    float ramp = clamp(
        (a_position.z - u_fade_params.x) / (u_fade_params.y - u_fade_params.x), 0.0, 1.0);
    v_color0 = vec4(a_color0.rgb, a_color0.a * ramp);
}
