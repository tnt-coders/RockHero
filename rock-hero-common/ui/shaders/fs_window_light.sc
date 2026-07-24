$input v_color0, v_texcoord0

// Hand-window light: one continuous brightness calculation across the whole window width. The
// interpolated texcoord holds the fragment's pre-offset, pre-scaled distances inside the
// window's low and high edges (the renderer bakes the half-band centering offset and the
// per-slice morph scaling into the vertex values, so the fade's outer boundary stays at the
// settled spill everywhere while morph fades widen inward only); their minimum runs through a
// smoothstep over the falloff band (u_window_light_params.x).
#include <bgfx_shader.sh>

uniform vec4 u_window_light_params;

void main()
{
    float inside = min(v_texcoord0.x, v_texcoord0.y);
    float mask = smoothstep(0.0, u_window_light_params.x, inside);
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * mask);
}
