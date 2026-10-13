// The block pass of a look restricted to the theme's colors: each virtual pixel's mean color snaps
// to the nearest color the theme states, as a limited-palette board's video output could show
// nothing else. Snapping here, once per virtual pixel, puts it where the board's palette sat:
// before the present's scanlines and beam softening, which act on the screen, not the board.
//
// The dither look first nudges each mean by a 4x4 Bayer threshold, so flat areas between two of the
// theme's colors break into the ordered cross-hatch of a limited-palette display; the other looks
// pass a spread of 0 and snap plainly.
//
// Nearest is the smallest squared RGB distance, the earlier entry winning a tie: the rule
// nearestThemeColor states on the CPU, which the guardrails judge a snapping look's colors by.
#include <bgfx_shader.sh>
#include "present_blocks.sh"

// The palette, rgb from 0 to 1; only the first u_present_params.z entries are live.
uniform vec4 u_present_palette[32];

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
    // y: how far the Bayer threshold pushes a color, in RGB from 0 to 1; z: live palette entries.
    vec2 virtual_pixel = floor(gl_FragCoord.xy);
    float threshold = (bayer4(virtual_pixel) + 0.5) / 16.0 - 0.5;
    vec3 nudged = blockMean(gl_FragCoord.xy).rgb + vec3_splat(threshold * u_present_params.y);

    vec3 nearest = u_present_palette[0].rgb;
    vec3 first_gap = nearest - nudged;
    float nearest_gap = dot(first_gap, first_gap);
    for (int index = 1; index < 32; ++index)
    {
        if (float(index) >= u_present_params.z)
        {
            break;
        }
        vec3 gap = u_present_palette[index].rgb - nudged;
        float squared = dot(gap, gap);
        if (squared < nearest_gap)
        {
            nearest_gap = squared;
            nearest = u_present_palette[index].rgb;
        }
    }
    gl_FragColor = vec4(nearest, 1.0);
}
