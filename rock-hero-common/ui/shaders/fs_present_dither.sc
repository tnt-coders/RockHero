// The dither look's present: each virtual pixel of the block canvas (present_blocks), blown up
// like the pixel look's, takes the nearest color of the theme's own opaque palette after a 4x4
// Bayer threshold nudges it, so flat areas between two palette colors break into the ordered
// cross-hatch of a limited-palette display.
//
// The palette is every color the theme states, alpha dropped, plus black and white
// (makeHighwayDitherPalette), so the dither shows nothing the theme's guardrails did not approve.
#include <bgfx_shader.sh>

// One texel per virtual pixel: the block canvas, or the scene itself at scale 1.
SAMPLER2D(s_scene, 0);

// x: window pixels per virtual pixel; z: how many palette entries are live.
uniform vec4 u_present_params;

// The palette, rgb from 0 to 1; only the first u_present_params.z entries are live.
uniform vec4 u_dither_palette[32];

// How far the Bayer threshold pushes a color, in RGB from 0 to 1, before it snaps to the palette.
#define DITHER_SPREAD 0.25

// The 2x2 Bayer matrix [[0, 2], [3, 1]] at one cell, its coordinates 0 or 1.
float bayer2(vec2 bits)
{
    return 2.0 * abs(bits.x - bits.y) + bits.y;
}

// The 4x4 Bayer matrix, 0 to 15, built recursively from the 2x2.
float bayer4(vec2 pixel)
{
    vec2 low = mod(pixel, 2.0);
    vec2 high = mod(floor(pixel / 2.0), 2.0);
    return 4.0 * bayer2(low) + bayer2(high);
}

void main()
{
    vec2 virtual_pixel = floor(gl_FragCoord.xy / u_present_params.x);
    vec3 color = texelFetch(s_scene, ivec2(virtual_pixel), 0).rgb;
    float threshold = (bayer4(virtual_pixel) + 0.5) / 16.0 - 0.5;
    vec3 nudged = color + vec3_splat(threshold * DITHER_SPREAD);

    vec3 nearest = u_dither_palette[0].rgb;
    vec3 first_gap = nearest - nudged;
    float nearest_gap = dot(first_gap, first_gap);
    for (int index = 1; index < 32; ++index)
    {
        if (float(index) >= u_present_params.z)
        {
            break;
        }
        vec3 gap = u_dither_palette[index].rgb - nudged;
        float squared = dot(gap, gap);
        if (squared < nearest_gap)
        {
            nearest_gap = squared;
            nearest = u_dither_palette[index].rgb;
        }
    }
    gl_FragColor = vec4(nearest, 1.0);
}
