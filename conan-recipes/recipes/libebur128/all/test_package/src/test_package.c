#include <ebur128.h>

int main(void)
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    ebur128_get_version(&major, &minor, &patch);

    ebur128_state* state = ebur128_init(2, 48000, EBUR128_MODE_I);
    if (state == 0)
    {
        return 1;
    }

    ebur128_destroy(&state);
    return state == 0 && major == 1 && minor == 2 && patch == 6 ? 0 : 1;
}
