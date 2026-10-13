// The CRT present: carries the off-screen scene to the window as an arcade raster monitor shows
// its picture, tuned so every note, lane and fret number stays as legible as on the plain board.
//
// - A fixed count of scan lines over the window's height, as a 15 kHz monitor shows about 240
//   visible lines whatever the tube's size. Each line's color is a tent-weighted mean of the
//   scene's rows within one line pitch of its center, so a small far note moving down the board
//   crossfades from one line into the next as it moves, instead of sitting on a line and then
//   hopping a whole pitch; columns keep the scene's full resolution, so text stays sharp.
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

// Visible scan lines over the window's height.
#define CRT_LINES 240.0

// The narrowest line pitch in window pixels: below two, the gaps alias into moire on a small
// window.
#define CRT_MIN_PITCH 2.0

// The beam's sharpness, as the exponent a squared distance in line pitches is scaled by: a black
// line's beam is the hardest, a full-bright line's the widest.
#define CRT_BEAM_DARK 8.0
#define CRT_BEAM_BRIGHT 4.0

// What the mask keeps of the two channels a column's stripe does not light, and the gain that
// restores the mean it removes: (1 + 2 * 0.8) / 3 * 1.15 is about 1.
#define CRT_MASK_DIM 0.8
#define CRT_MASK_GAIN 1.15

// The display gamma the scene's colors are encoded with.
#define CRT_GAMMA 2.2

// ln(2) / pi: a beam exp2(-h * d * d) holds sqrt(pi / (h * ln(2))) of light over its whole profile.
#define CRT_LN2_OVER_PI 0.22063560

// One scene sample in linear light, at window pixel coordinates; at a row's center it reads that
// row alone. Sampled at an explicit level because lineColor's trip count varies per fragment,
// where a gradient sample is illegal.
vec3 linearScene(vec2 pixel)
{
    vec3 encoded = texture2DLod(s_scene, pixel * u_viewTexel.xy, 0.0).rgb;
    return pow(encoded, vec3_splat(CRT_GAMMA));
}

// The color of scan line `index` at column x: the rows within one pitch of its center, weighted
// by a tent that falls from 1 at the center to 0 a pitch away. Neighboring lines' tents sum to 1
// at every row, so each row's light is shared between the two lines around it in proportion to
// its distance from each, and a moving note's light slides from line to line with it.
vec3 lineColor(float index, float pitch, float x)
{
    float center = (index + 0.5) * pitch;
    float first = ceil(center - pitch - 0.5);
    int rows = int(floor(center + pitch - 0.5) - first) + 1;
    vec3 sum = vec3_splat(0.0);
    float weights = 0.0;
    for (int row = 0; row < rows; ++row)
    {
        float y = first + float(row) + 0.5;
        float weight = max(1.0 - abs(y - center) / pitch, 0.0);
        sum += weight * linearScene(vec2(x, y));
        weights += weight;
    }
    return sum / weights;
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
    float pitch = max(u_viewRect.w / CRT_LINES, CRT_MIN_PITCH);
    float x = gl_FragCoord.x;

    // Line k is centered at k on this axis, so the fragment lies between line `previous` and the
    // next one, a fraction `offset` of a pitch past the former.
    float position = gl_FragCoord.y / pitch - 0.5;
    float previous = floor(position);
    float offset = position - previous;
    vec3 previous_color = lineColor(previous, pitch, x);
    vec3 next_color = lineColor(previous + 1.0, pitch, x);
    vec3 light = previous_color * beam(previous_color, offset)
                 + next_color * beam(next_color, 1.0 - offset);

    // Column 0 of each stripe triad lights red, 1 green, 2 blue.
    float stripe = mod(floor(x), 3.0);
    vec3 lit_channels = step(abs(vec3(0.0, 1.0, 2.0) - stripe), vec3_splat(0.5));
    light *= mix(vec3_splat(CRT_MASK_DIM), vec3_splat(1.0), lit_channels) * CRT_MASK_GAIN;

    gl_FragColor = vec4(pow(clamp(light, 0.0, 1.0), vec3_splat(1.0 / CRT_GAMMA)), 1.0);
}
