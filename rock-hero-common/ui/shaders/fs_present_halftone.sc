// The halftone look's present: comic print. The window is cut into a dot screen turned 45
// degrees; each cell takes the scene's color at its center, and its dot grows with that color's
// luminance (area in proportion, a full-white cell covered corner to corner) over the theme's
// backdrop, which shows between the dots. Dark ink then outlines every luminance edge of the
// full-resolution scene, found with a Sobel filter at each window pixel.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// x: the screen cell's edge in window pixels; zw: the scene's size, which is the window's.
uniform vec4 u_present_params;

// The theme's backdrop, rgb.
uniform vec4 u_present_backdrop;

// Where an edge starts to ink and where it inks fully, in Sobel magnitude of 0-1 luminance.
#define INK_LOW 0.2
#define INK_HIGH 0.6

// The ink: a near-black, so an outline over the backdrop still reads as drawn.
#define INK_COLOR vec3(0.03, 0.03, 0.04)

// Half the square root of two: the 45-degree turn's sine and cosine.
#define HALF_ROOT2 0.70710678

// Rec. 709 luminance of a 0-1 color.
float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// The scene's color at a window pixel, clamped to the scene.
vec3 sceneAt(vec2 pixel)
{
    vec2 inside = clamp(pixel, vec2_splat(0.0), u_present_params.zw - vec2_splat(1.0));
    return texelFetch(s_scene, ivec2(inside), 0).rgb;
}

// The scene's luminance at the pixel offset from `pixel` by (dx, dy).
float lumaAt(vec2 pixel, float dx, float dy)
{
    return luminance(sceneAt(pixel + vec2(dx, dy)));
}

void main()
{
    float cell = u_present_params.x;
    vec2 pixel = floor(gl_FragCoord.xy);

    // Into the turned screen, in cells: rotate by -45 degrees and divide by the cell's edge.
    vec2 turned = vec2(pixel.x + pixel.y, pixel.y - pixel.x) * (HALF_ROOT2 / cell);
    vec2 cell_index = floor(turned);
    vec2 offset = turned - cell_index - vec2_splat(0.5);
    // The cell's center back in window pixels: the inverse turn of its turned center.
    vec2 turned_center = (cell_index + vec2_splat(0.5)) * cell;
    vec2 center = vec2(turned_center.x - turned_center.y, turned_center.x + turned_center.y)
                * HALF_ROOT2;

    vec3 cell_color = sceneAt(floor(center));
    float radius = HALF_ROOT2 * sqrt(luminance(cell_color));
    float soft = 1.0 / cell;
    float coverage = 1.0 - smoothstep(radius - soft, radius + soft, length(offset));
    vec3 printed = mix(u_present_backdrop.rgb, cell_color, coverage);

    float top_left = lumaAt(pixel, -1.0, 1.0);
    float top = lumaAt(pixel, 0.0, 1.0);
    float top_right = lumaAt(pixel, 1.0, 1.0);
    float left = lumaAt(pixel, -1.0, 0.0);
    float right = lumaAt(pixel, 1.0, 0.0);
    float bottom_left = lumaAt(pixel, -1.0, -1.0);
    float bottom = lumaAt(pixel, 0.0, -1.0);
    float bottom_right = lumaAt(pixel, 1.0, -1.0);
    float across = (top_right + 2.0 * right + bottom_right) - (top_left + 2.0 * left + bottom_left);
    float along = (top_left + 2.0 * top + top_right) - (bottom_left + 2.0 * bottom + bottom_right);
    float ink = smoothstep(INK_LOW, INK_HIGH, length(vec2(across, along)));

    gl_FragColor = vec4(mix(printed, INK_COLOR, ink), 1.0);
}
