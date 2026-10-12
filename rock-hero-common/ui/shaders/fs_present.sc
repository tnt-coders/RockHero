// The full-screen present: copies the off-screen scene to the window through the render look.
//
// s_scene holds one texel per virtual pixel: the scene itself for the plain look, where a virtual
// pixel is a window pixel and the copy is exact, and the block canvas (present_blocks) for the
// pixel look, where each texel becomes one hard-edged block of u_present_params.x window pixels.
// Texels are FETCHED by index, so the copy never filters, and indexing by gl_FragCoord needs no
// per-backend flip: the window and the render target share one row origin on every backend.
//
// Scanlines darken every other virtual-pixel row by u_present_params.y. They darken fixed rows, so
// a small note moving down the board would pulse as it crossed from a bright row to a dark one;
// with scanlines on, each virtual pixel therefore spreads 1:2:1 over the rows above and below —
// exactly one scanline period, the soft vertical beam of a real display — so a moving note always
// lies half on each and its brightness holds. Without scanlines nothing pulses, and the copy stays
// crisp.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// x: window pixels per virtual pixel; y: scanline strength, 0 to 1; z: rows of s_scene.
uniform vec4 u_present_params;

void main()
{
    vec2 virtual_pixel = floor(gl_FragCoord.xy / u_present_params.x);
    ivec2 texel = ivec2(virtual_pixel);
    vec4 color = texelFetch(s_scene, texel, 0);
    if (u_present_params.y > 0.0)
    {
        // The edge rows repeat themselves rather than read past the canvas.
        int last_row = int(u_present_params.z) - 1;
        vec4 above = texelFetch(s_scene, ivec2(texel.x, max(texel.y - 1, 0)), 0);
        vec4 below = texelFetch(s_scene, ivec2(texel.x, min(texel.y + 1, last_row)), 0);
        color = 0.5 * color + 0.25 * (above + below);
    }
    float scanline = u_present_params.y * mod(virtual_pixel.y, 2.0);
    gl_FragColor = vec4(color.rgb * (1.0 - scanline), color.a);
}
