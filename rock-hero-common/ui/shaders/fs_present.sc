// The full-screen present: copies the off-screen scene to the window through the render look.
//
// Each window pixel shows the mean of its block (present_block.sh): with u_present_params.x at 1
// (the plain look) a block is one texel and the copy is exact; above it (the pixel look) every
// block becomes one hard-edged virtual pixel, its color supersampled from the full-resolution
// scene.
//
// Scanlines darken every other BLOCK row by u_present_params.y, so a scanline is one virtual pixel
// tall; the plain look's zero leaves every row as rendered.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

#include "present_block.sh"

// x: window pixels per virtual pixel; y: scanline strength, 0 to 1.
uniform vec4 u_present_params;

void main()
{
    vec2 block = presentBlock(gl_FragCoord.xy, u_present_params.x);
    vec4 color = presentBlockMean(block, u_present_params.x);
    float scanline = u_present_params.y * mod(block.y, 2.0);
    gl_FragColor = vec4(color.rgb * (1.0 - scanline), color.a);
}
