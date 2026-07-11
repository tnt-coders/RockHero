$input v_color0

// Flat vertex-color surface program: passes the interpolated vertex color through unchanged.
#include <bgfx_shader.sh>

void main()
{
    gl_FragColor = v_color0;
}
