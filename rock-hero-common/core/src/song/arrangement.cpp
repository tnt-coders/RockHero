#include <rock_hero/common/core/song/arrangement.h>

namespace rock_hero::common::core
{

std::string_view partToken(const Part part) noexcept
{
    switch (part)
    {
        case Part::Lead:
            return "Lead";
        case Part::Rhythm:
            return "Rhythm";
        case Part::Bass:
            return "Bass";
    }

    // Unreachable for a valid enumerator (-Wswitch flags any new part added to the enum). The
    // fallback stays the default part's token so even a corrupted value serializes to a valid
    // song-document token rather than an empty one.
    return "Lead";
}

std::optional<Part> parsePartToken(const std::string_view token) noexcept
{
    if (token == "Lead")
    {
        return Part::Lead;
    }

    if (token == "Rhythm")
    {
        return Part::Rhythm;
    }

    if (token == "Bass")
    {
        return Part::Bass;
    }

    return std::nullopt;
}

} // namespace rock_hero::common::core
