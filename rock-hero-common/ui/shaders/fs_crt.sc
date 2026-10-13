// The CRT present: carries the off-screen scene to the window as an arcade raster monitor shows
// its picture. The monitor is one of the presets the renderer defines (crtParams in
// highway_renderer.cpp), passed in u_crt_params; everything every monitor shares is stated here.
//
// - Scan lines a whole number of window pixels apart, about u_crt_params.x of them over the
//   window's height. The whole-pixel spacing lands every line on the pixel grid alike, so no line
//   reads softer than its neighbors and no beat pattern runs down the board.
// - A smooth monitor (u_crt_params.y = 0) takes each line's color as a tent-weighted mean of the
//   scene's rows within one line pitch of its center, so a small far note moving down the board
//   crossfades from one line into the next as it moves instead of hopping a whole pitch; columns
//   keep the scene's full resolution, so text stays sharp.
// - A pixel board (u_crt_params.y = 1) instead shows square pixels one line pitch wide, a power of
//   two in window pixels, each the exact mean of the scene block it covers: one read of the mip
//   level whose texels are that block. Pixels keep hard edges and step as things move, as an early
//   board's sprites did; each row of pixels is one scan line.
// - A board with a color limit (u_crt_board, the steps each of red, green and blue may take, 0 for
//   none) rounds each line's encoded color to its levels, as an early arcade board's video output
//   could show nothing else: one bit a channel gives the first color boards' eight colors, and
//   3-3-2 bits the 256 of the boards that followed.
// - Each line is sharpened against its two neighbors, as a monitor's sharpness control peaks its
//   picture: the line gains u_crt_params.z times its difference from their mean. The correction is
//   symmetric and sums to zero, so it changes neither a note's total light nor where that light is
//   centered.
// - Each line is drawn as a beam of Gaussian profile whose width grows with its brightness: dark
//   and mid areas show the dark gaps between lines, while bright heads, labels and lit lines bloom
//   until their gaps close. Every beam carries exactly its line's light, as a real beam's light
//   follows its current while its spot widens, so a note keeps its brightness whichever lines
//   share it and the board keeps the plain look's brightness. A fragment sums the two lines
//   nearest it.
// - A gentle RGB stripe mask, one phosphor stripe per window column, which every column keeps at
//   least CRT_MASK_DIM of every channel through, so a one-pixel white stroke tints instead of
//   fringing; the gain gives back the mean the mask takes.
//
// The beam and mask work in linear light. The scene target and the window share one row origin on
// every backend, so the window's fragment coordinates index the scene directly, with no flip.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// x: scan lines over the window's height; y: 1 for a pixel board, 0 for a smooth monitor; z: how
// strongly each line is peaked against its neighbors.
uniform vec4 u_crt_params;

// xyz: the steps red, green and blue may take, as a channel's levels minus one, 0 for no limit;
// w: where a channel rounds up to its next level, as a fraction of a step from the one below
// (0.5 rounds to the nearest; less lifts dim colors the board would otherwise drop to black).
uniform vec4 u_crt_board;

// The narrowest line pitch in window pixels: below two, the gaps alias into moire on a small
// window.
#define CRT_MIN_PITCH 2.0

// The beam's sharpness, as the exponent a squared distance in line pitches is scaled by: a black
// line's beam is the hardest, a full-bright line's the widest.
#define CRT_BEAM_DARK 12.0
#define CRT_BEAM_BRIGHT 4.0

// What the mask keeps of the two channels a column's stripe does not light, and the gain that
// restores the mean it removes: (1 + 2 * 0.8) / 3 * 1.15 is about 1.
#define CRT_MASK_DIM 0.8
#define CRT_MASK_GAIN 1.15

// The display gamma the scene's colors are encoded with.
#define CRT_GAMMA 2.2

// ln(2) / pi: a beam exp2(-h * d * d) holds sqrt(pi / (h * ln(2))) of light over its whole profile.
#define CRT_LN2_OVER_PI 0.22063560

// One scene sample in linear light, at window pixel coordinates and a mip level. At a row's center
// on level 0 it reads that row alone; at a block's center on its own level, that block's mean.
// Sampled at an explicit level because lineColor's trip count varies per fragment, where a
// gradient sample is illegal.
vec3 linearScene(vec2 pixel, float level)
{
    vec3 encoded = texture2DLod(s_scene, pixel * u_viewTexel.xy, level).rgb;
    return pow(encoded, vec3_splat(CRT_GAMMA));
}

// A line's color as the board outputs it: unchanged, or its encoded value rounded to the nearest
// level the board's color limit has.
vec3 boardColor(vec3 color)
{
    if (u_crt_board.x == 0.0)
    {
        return color;
    }
    vec3 steps = u_crt_board.xyz;
    vec3 encoded = pow(color, vec3_splat(1.0 / CRT_GAMMA));
    vec3 held = floor(encoded * steps + (1.0 - u_crt_board.w)) / steps;
    return pow(held, vec3_splat(CRT_GAMMA));
}

// The color of scan line `index`, centered at window row `center`, at column x. A pixel board
// reads the mean of the block the column falls in from mip level `level`; a smooth monitor weighs
// the rows within one pitch of the center by a tent that falls from 1 at the center to 0 a pitch
// away, whose neighbors sum to 1 at every row, so a moving note's light slides from line to line.
vec3 lineColor(float center, float pitch, float x, float level)
{
    if (u_crt_params.y != 0.0)
    {
        return boardColor(linearScene(vec2((floor(x / pitch) + 0.5) * pitch, center), level));
    }
    float first = ceil(center - pitch - 0.5);
    int rows = int(floor(center + pitch - 0.5) - first) + 1;
    vec3 sum = vec3_splat(0.0);
    float weights = 0.0;
    for (int row = 0; row < rows; ++row)
    {
        float y = first + float(row) + 0.5;
        float weight = max(1.0 - abs(y - center) / pitch, 0.0);
        sum += weight * linearScene(vec2(x, y), 0.0);
        weights += weight;
    }
    return boardColor(sum / weights);
}

// A line's color peaked against the lines on either side of it. A line beside a much brighter one
// would go below black, which no beam can show, so it stops at black.
vec3 sharpened(vec3 scan_line, vec3 one_side, vec3 other_side)
{
    vec3 peaked = scan_line + u_crt_params.z * (scan_line - 0.5 * (one_side + other_side));
    return max(peaked, vec3_splat(0.0));
}

// A line's beam at a distance from its center, in line pitches, per channel: wider the brighter
// the line, and scaled so its whole profile holds one pitch of light, which the line's color then
// sets.
vec3 beam(vec3 color, float pitches)
{
    vec3 sharpness = mix(vec3_splat(CRT_BEAM_DARK), vec3_splat(CRT_BEAM_BRIGHT), color);
    return exp2(-sharpness * pitches * pitches) * sqrt(sharpness * CRT_LN2_OVER_PI);
}

void main()
{
    // A pixel board's pitch is the power of two nearest the window's rows per line, so its pixels
    // are mip texels; a smooth monitor's is the nearest whole number of rows. Either way a line is
    // centered on the middle of its rows: a pixel's center, or a row's, so the beam peaks there.
    float rows_per_line = u_viewRect.w / u_crt_params.x;
    float level = 0.0;
    float pitch = max(floor(rows_per_line + 0.5), CRT_MIN_PITCH);
    float phase = floor(pitch * 0.5) + 0.5;
    if (u_crt_params.y != 0.0)
    {
        level = max(floor(log2(rows_per_line) + 0.5), 1.0);
        pitch = exp2(level);
        phase = pitch * 0.5;
    }
    float x = gl_FragCoord.x;

    // Line k is centered at k on this axis, so the fragment lies between line `previous` and the
    // next one, a fraction `offset` of a pitch past the former.
    float position = (gl_FragCoord.y - phase) / pitch;
    float previous = floor(position);
    float offset = position - previous;
    vec3 before_previous = lineColor((previous - 1.0) * pitch + phase, pitch, x, level);
    vec3 previous_line = lineColor(previous * pitch + phase, pitch, x, level);
    vec3 next_line = lineColor((previous + 1.0) * pitch + phase, pitch, x, level);
    vec3 after_next = lineColor((previous + 2.0) * pitch + phase, pitch, x, level);
    vec3 previous_color = sharpened(previous_line, before_previous, next_line);
    vec3 next_color = sharpened(next_line, previous_line, after_next);
    vec3 light = previous_color * beam(previous_color, offset)
                 + next_color * beam(next_color, 1.0 - offset);

    // Column 0 of each stripe triad lights red, 1 green, 2 blue.
    float stripe = mod(floor(x), 3.0);
    vec3 lit_channels = step(abs(vec3(0.0, 1.0, 2.0) - stripe), vec3_splat(0.5));
    light *= mix(vec3_splat(CRT_MASK_DIM), vec3_splat(1.0), lit_channels) * CRT_MASK_GAIN;

    gl_FragColor = vec4(pow(clamp(light, 0.0, 1.0), vec3_splat(1.0 / CRT_GAMMA)), 1.0);
}
