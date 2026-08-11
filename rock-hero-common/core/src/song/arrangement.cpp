#include "song/arrangement.h"

#include <algorithm>

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
    // Derived from partToken rather than restating its three literals: the inverse of a mapping
    // cannot be written out a second time without eventually disagreeing with the first.
    const auto match = std::ranges::find_if(
        g_parts, [token](const Part part) { return partToken(part) == token; });
    if (match == g_parts.end())
    {
        return std::nullopt;
    }

    return *match;
}

} // namespace rock_hero::common::core
