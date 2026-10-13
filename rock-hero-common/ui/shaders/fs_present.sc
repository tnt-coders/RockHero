// The full-screen present: copies the off-screen scene to the window through the render look.
//
// s_scene holds one texel per virtual pixel: the scene itself for the plain and CRT looks, where a
// virtual pixel is a window pixel and the copy is exact, and the block canvas (present_blocks) for
// the pixel look, where each texel becomes one hard-edged block of u_present_params.x window
// pixels. Texels are FETCHED by index, so the copy never filters, and indexing by gl_FragCoord
// needs no per-backend flip: the window and the render target share one row origin on every
// backend.
//
// Scanlines darken every other band of u_present_params.w texel rows by u_present_params.y: one
// virtual pixel under the pixel look, the look's own line height under the CRT. They darken fixed
// rows, so a small note moving down the board would pulse as it crossed from a bright band to a
// dark one. With scanlines on, each texel therefore spreads over exactly one scanline period — the
// soft vertical beam of a real display — weighting the rows within a band of it 1 and the two a
// band away 1/2: every row then sends half its light to bright bands and half to dark, so a moving
// note's brightness holds. A band of one row makes that 1:2:1. Rows only: every column stays
// sharp, and without scanlines nothing pulses and the copy stays crisp.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// x: window pixels per virtual pixel; y: scanline strength, 0 to 1; z: rows of s_scene; w: rows of
// s_scene per scanline band.
uniform vec4 u_present_params;

void main()
{
    ivec2 texel = ivec2(floor(gl_FragCoord.xy / u_present_params.x));
    vec4 color = texelFetch(s_scene, texel, 0);
    if (u_present_params.y > 0.0)
    {
        // The edge rows repeat themselves rather than read past s_scene.
        int last_row = int(u_present_params.z) - 1;
        int radius = int(u_present_params.w);
        vec4 sum = 0.5 * (texelFetch(s_scene, ivec2(texel.x, max(texel.y - radius, 0)), 0)
                          + texelFetch(s_scene, ivec2(texel.x, min(texel.y + radius, last_row)), 0));
        for (int offset = 1 - radius; offset < radius; ++offset)
        {
            sum += texelFetch(s_scene, ivec2(texel.x, clamp(texel.y + offset, 0, last_row)), 0);
        }
        color = sum / float(2 * radius);
    }
    float band = floor(gl_FragCoord.y / (u_present_params.x * u_present_params.w));
    float scanline = u_present_params.y * mod(band, 2.0);
    gl_FragColor = vec4(color.rgb * (1.0 - scanline), color.a);
}
