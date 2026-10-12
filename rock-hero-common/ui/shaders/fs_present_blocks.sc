// The pixel and dither looks' block pass: one texel per virtual pixel, each the color of the
// scale x scale block of the full-resolution scene it covers. Rendered into the block canvas, one
// fragment per block, so every block's color is computed once however many window pixels show it.
//
// A block's color is the MEAN of its scene texels, so a note smaller than a block tints it by the
// share it covers and moves through it smoothly instead of popping in and out as it crosses a
// single sample.
//
// Texels are FETCHED by index, so nothing filters across a block's edge, and indexing by
// gl_FragCoord needs no per-backend flip: every target shares one row origin on every backend. The
// scene is sized in whole blocks, so every fetch lands inside it.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// x: scene texels per block edge.
uniform vec4 u_present_params;

void main()
{
    int scale = int(u_present_params.x);
    ivec2 origin = ivec2(floor(gl_FragCoord.xy)) * scale;
    vec4 sum = vec4_splat(0.0);
    for (int row = 0; row < scale; ++row)
    {
        for (int column = 0; column < scale; ++column)
        {
            sum += texelFetch(s_scene, origin + ivec2(column, row), 0);
        }
    }
    gl_FragColor = sum / float(scale * scale);
}
