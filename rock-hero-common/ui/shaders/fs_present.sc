// The full-screen present: copies the off-screen scene to the window through the render look.
//
// Each window pixel FETCHES its scene texel by index rather than sampling by coordinate, so the
// copy never filters: with one window pixel per scene pixel (the plain look) it is exact, and
// with u_present_params.x window pixels per scene pixel (the pixel look) every scene pixel becomes
// one hard-edged block. Indexing by gl_FragCoord also needs no per-backend flip: the window and
// the render target share one row origin on every backend, top-left on Direct3D and Metal and
// bottom-left on OpenGL.
//
// Scanlines darken every other SCENE row by u_present_params.y, so a scanline is one virtual
// pixel tall; the plain look's zero leaves every row as rendered.
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

// x: window pixels per scene pixel; y: scanline strength, 0 to 1.
uniform vec4 u_present_params;

void main()
{
    vec2 scene_pixel = floor(gl_FragCoord.xy / u_present_params.x);
    vec4 color = texelFetch(s_scene, ivec2(scene_pixel), 0);
    float scanline = u_present_params.y * mod(scene_pixel.y, 2.0);
    gl_FragColor = vec4(color.rgb * (1.0 - scanline), color.a);
}
