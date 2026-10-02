#include <cctype>
#include <charconv>
#include <cmath>
#include <rock_hero/common/audio/shared/pan.h>
#include <system_error>

namespace rock_hero::common::audio
{

namespace
{

// The display unit: whole percent of full deflection, so a position of 0.35 prints as 35.
constexpr double g_percent_per_unit{100.0};

// Trims ASCII whitespace from both ends so typed entry tolerates surrounding spaces.
[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    while (!text.empty() && is_space(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return text;
}

// Reads a whole non-negative percent that must consume the entire text; the display form carries
// whole percents only, so an integer parse is its exact inverse and stays portable (libc++ ships
// no floating-point from_chars).
[[nodiscard]] std::optional<int> wholePercent(std::string_view digits) noexcept
{
    int percent{0};
    const auto [parsed_to, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), percent);
    if (error != std::errc{} || parsed_to != digits.data() + digits.size() || percent < 0)
    {
        return std::nullopt;
    }
    return percent;
}

} // namespace

std::string formatPan(Pan pan)
{
    const long percent = std::lround(clampPan(pan).position * g_percent_per_unit);
    if (percent == 0)
    {
        return "C";
    }
    return (percent < 0 ? "L" : "R") + std::to_string(std::labs(percent));
}

std::optional<Pan> parsePan(std::string_view text)
{
    const std::string_view body = trimmed(text);
    if (body.empty())
    {
        return std::nullopt;
    }

    const auto side = static_cast<char>(std::toupper(static_cast<unsigned char>(body.front())));
    if (side == 'C' && body.size() == 1)
    {
        return Pan{};
    }
    if (side != 'L' && side != 'R')
    {
        return std::nullopt;
    }
    const std::optional<int> percent = wholePercent(trimmed(body.substr(1)));
    if (!percent.has_value())
    {
        return std::nullopt;
    }
    const double magnitude = static_cast<double>(*percent) / g_percent_per_unit;
    return clampPan(Pan{side == 'L' ? -magnitude : magnitude});
}

} // namespace rock_hero::common::audio
