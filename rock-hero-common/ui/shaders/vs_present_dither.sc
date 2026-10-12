$input a_position

// The dither look's present: the same full-screen triangle as vs_present (every program owns its
// vertex stage by name). The triangle, already in clip space: it covers the whole window on
// every backend, so the vertex stage applies no transform and hands the fragment stage nothing.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
}
