$input v_color0, v_texcoord0

// Hand-window light: one continuous brightness calculation across the whole window width. The
// interpolated texcoord holds the fragment's distances inside the window's low and high edges;
// their minimum runs through a smoothstep over the falloff band (u_window_light_params.x),
// centered ON each edge (user tuning 2026-07-23: this placement gives the lit region its full
// width, reaching the edges with only a soft half-band spill): half strength exactly at the
// edge, gone half a band outside, full half a band inside.
#include <bgfx_shader.sh>

uniform vec4 u_window_light_params;

void main()
{
    float inside = min(v_texcoord0.x, v_texcoord0.y);
    float mask =
        smoothstep(0.0, u_window_light_params.x, inside + (0.5 * u_window_light_params.x));
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * mask);
}
