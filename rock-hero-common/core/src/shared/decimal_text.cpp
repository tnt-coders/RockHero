#include "shared/decimal_text.h"

#include <cmath>
#include <juce_core/juce_core.h>

namespace rock_hero::common::core
{

// juce::CharacterFunctions is as locale-independent as std::from_chars would be, and the cursor it
// advances gives the same whole-string strictness check.
std::optional<double> parseFiniteDecimal(const std::string_view text)
{
    const juce::String trimmed =
        juce::String::fromUTF8(text.data(), static_cast<int>(text.size())).trim();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }

    juce::String::CharPointerType cursor = trimmed.getCharPointer();
    const double value = juce::CharacterFunctions::readDoubleValue(cursor);
    if (!cursor.isEmpty() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

} // namespace rock_hero::common::core
