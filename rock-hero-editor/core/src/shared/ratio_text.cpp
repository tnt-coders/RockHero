#include "shared/ratio_text.h"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace rock_hero::editor::core
{

namespace
{

// Spaces and tabs only: a prompt or a stored token is one line of text.
[[nodiscard]] std::string_view trimmed(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

} // namespace

// from_chars reads no sign, no space and no decimal point, and the end check refuses anything left
// over.
std::optional<int> parsePositiveWhole(const std::string_view text)
{
    const std::string_view digits = trimmed(text);
    int value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size() ||
        value <= 0)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<TypedRatio> parseTypedRatio(const std::string_view text)
{
    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::optional<int> numerator = parsePositiveWhole(text.substr(0, slash));
    const std::optional<int> denominator = parsePositiveWhole(text.substr(slash + 1));
    if (!numerator.has_value() || !denominator.has_value())
    {
        return std::nullopt;
    }
    return TypedRatio{.numerator = *numerator, .denominator = *denominator};
}

} // namespace rock_hero::editor::core
