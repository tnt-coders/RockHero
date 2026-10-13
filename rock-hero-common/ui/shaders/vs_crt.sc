$input a_position

// The CRT present's one triangle, already in clip space: it covers the whole window on every
// backend, so the vertex stage applies no transform and hands the fragment stage nothing.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
}
