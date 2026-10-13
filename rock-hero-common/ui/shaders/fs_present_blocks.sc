// The block pass: one texel per virtual pixel, each the color of the scale x scale block of the
// full-resolution scene it covers. Rendered into the block canvas, one fragment per block, so every
// block's color is computed once however many window pixels show it.
#include <bgfx_shader.sh>
#include "present_blocks.sh"

void main()
{
    gl_FragColor = blockMean(gl_FragCoord.xy);
}
