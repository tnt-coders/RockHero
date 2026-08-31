$input v_color0, v_texcoord0

// Distance-faded vertex color: alpha ramps linearly on world Z between u_fade_params.x (fully
// transparent) and u_fade_params.y (fully opaque). Applying it here keeps the distance fade
// separate from any x-taper already carried in the interpolated vertex color.
#include <bgfx_shader.sh>

uniform vec4 u_fade_params;

void main()
{
    float ramp = clamp(
        (v_texcoord0.x - u_fade_params.x) / (u_fade_params.y - u_fade_params.x), 0.0, 1.0);
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * ramp);
}
