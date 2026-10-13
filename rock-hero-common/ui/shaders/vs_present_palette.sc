$input a_position

// The palette block pass: the same full-screen triangle as vs_present (every program owns its
// vertex stage by name), here covering the block canvas. The triangle, already in clip space: it
// covers the whole target on every backend, so the vertex stage applies no transform and hands the
// fragment stage nothing.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
}
