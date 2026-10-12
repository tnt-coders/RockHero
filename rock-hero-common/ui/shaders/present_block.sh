#ifndef ROCK_HERO_PRESENT_BLOCK_SH
#define ROCK_HERO_PRESENT_BLOCK_SH

// The block a window pixel belongs to and that block's color, shared by the copy and the dither.
//
// The scene renders at the window's full resolution, multisampled, so a block of scale x scale
// scene texels is one virtual pixel: its color is the MEAN of every texel it covers. A note
// smaller than a block then tints its block by the share it covers and moves through it smoothly,
// where sampling one texel per block would pop the note in and out as it crossed the sample.
//
// Texels are FETCHED by index rather than sampled by coordinate, so nothing filters across a
// block's edge, and indexing by gl_FragCoord needs no per-backend flip: the window and the render
// target share one row origin on every backend. The scene target is sized in whole blocks
// (makeHighwaySceneFrame), so every fetch lands inside it.

// The largest scale the loops visit: g_pixel_look_max_scale and g_dither_look_max_scale.
#define PRESENT_MAX_SCALE 8

// The block holding this window pixel, in blocks; scale is window pixels per block.
vec2 presentBlock(vec2 frag_coord, float scale)
{
    return floor(frag_coord / scale);
}

// The mean of the scale x scale texels of s_scene the block covers; at scale 1, the block's one
// texel. The including program declares s_scene.
vec4 presentBlockMean(vec2 block, float scale)
{
    ivec2 origin = ivec2(block * scale);
    vec4 sum = vec4_splat(0.0);
    for (int row = 0; row < PRESENT_MAX_SCALE; ++row)
    {
        if (float(row) >= scale)
        {
            break;
        }
        for (int column = 0; column < PRESENT_MAX_SCALE; ++column)
        {
            if (float(column) >= scale)
            {
                break;
            }
            sum += texelFetch(s_scene, origin + ivec2(column, row), 0);
        }
    }
    return sum / (scale * scale);
}

#endif // ROCK_HERO_PRESENT_BLOCK_SH
