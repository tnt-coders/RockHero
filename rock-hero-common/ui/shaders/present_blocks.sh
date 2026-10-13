#ifndef ROCK_HERO_PRESENT_BLOCKS_SH
#define ROCK_HERO_PRESENT_BLOCKS_SH

// The block pass's one statement of a virtual pixel's color, shared by both block programs.
//
// A block's color is the MEAN of its scene texels, so a note smaller than a block tints it by the
// share it covers and moves through it smoothly instead of popping in and out as it crosses a
// single sample.
//
// Texels are FETCHED by index, so nothing filters across a block's edge, and indexing by
// gl_FragCoord needs no per-backend flip: every target shares one row origin on every backend. The
// scene is sized in whole blocks, so every fetch lands inside it.

SAMPLER2D(s_scene, 0);

// x: scene texels per block edge; the palette program reads y and z too.
uniform vec4 u_present_params;

// The mean color of the block one texel of the block canvas covers, given that texel's
// gl_FragCoord.xy: bgfx declares gl_FragCoord inside main alone on some backends.
vec4 blockMean(vec2 canvas_texel)
{
    int scale = int(u_present_params.x);
    ivec2 origin = ivec2(floor(canvas_texel)) * scale;
    vec4 sum = vec4_splat(0.0);
    for (int row = 0; row < scale; ++row)
    {
        for (int column = 0; column < scale; ++column)
        {
            sum += texelFetch(s_scene, origin + ivec2(column, row), 0);
        }
    }
    return sum / float(scale * scale);
}

#endif // ROCK_HERO_PRESENT_BLOCKS_SH
