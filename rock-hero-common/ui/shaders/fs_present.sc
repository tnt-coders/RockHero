// The full-screen present: copies the off-screen scene to the window through the render look.
//
// s_scene holds one texel per virtual pixel: the scene itself for the plain and CRT looks, where a
// virtual pixel is a window pixel and the copy is exact, and the block canvas (present_blocks) for
// the pixel look, where each texel becomes one hard-edged block of u_present_params.x window
// pixels. Texels are FETCHED by index, so the copy never filters, and indexing by gl_FragCoord
// needs no per-backend flip: the window and the render target share one row origin on every
// backend.
//
// Scanlines darken every other band of u_present_params.w window rows by u_present_params.y: one
// virtual pixel's height under the pixel look, the look's own line height under the CRT. They
// darken fixed rows, so a small note moving down the board would pulse as it crossed from a bright
// band to a dark one; on the block canvas each virtual pixel therefore spreads 1:2:1 over the rows
// above and below — exactly one scanline period, the soft vertical beam of a real display — so a
// moving note always lies half on each and its brightness holds. The CRT reads the scene and is
// never softened, keeping every window pixel sharp; without scanlines nothing pulses, and the copy
// stays crisp.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// x: window pixels per virtual pixel; y: scanline strength, 0 to 1; z: rows of the block canvas,
// 0 when s_scene is the scene; w: window rows per scanline band.
uniform vec4 u_present_params;

void main()
{
    ivec2 texel = ivec2(floor(gl_FragCoord.xy / u_present_params.x));
    vec4 color = texelFetch(s_scene, texel, 0);
    if (u_present_params.y > 0.0 && u_present_params.z > 0.0)
    {
        // The edge rows repeat themselves rather than read past the canvas.
        int last_row = int(u_present_params.z) - 1;
        vec4 above = texelFetch(s_scene, ivec2(texel.x, max(texel.y - 1, 0)), 0);
        vec4 below = texelFetch(s_scene, ivec2(texel.x, min(texel.y + 1, last_row)), 0);
        color = 0.5 * color + 0.25 * (above + below);
    }
    float band = floor(gl_FragCoord.y / u_present_params.w);
    float scanline = u_present_params.y * mod(band, 2.0);
    gl_FragColor = vec4(color.rgb * (1.0 - scanline), color.a);
}
