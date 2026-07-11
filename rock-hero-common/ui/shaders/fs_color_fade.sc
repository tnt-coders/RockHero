$input v_color0

// Distance-faded vertex color: the fade was applied per vertex, so this passes the interpolated
// color through unchanged.
#include <bgfx_shader.sh>

void main()
{
    gl_FragColor = v_color0;
}
