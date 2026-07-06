#include <charconv>
#include <rock_hero/common/core/chart/chart_tokens.h>

namespace rock_hero::common::core
{

namespace
{

// Parses a whole non-negative integer from the full span, rejecting partial matches.
[[nodiscard]] std::optional<int> parseWholeInt(const char* begin, const char* end)
{
    int value = 0;
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < 0)
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<GridPosition> parseGridPositionToken(const std::string& text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos)
    {
        return std::nullopt;
    }

    const auto measure = parseWholeInt(text.data(), text.data() + colon);
    if (!measure.has_value() || *measure <= 0)
    {
        return std::nullopt;
    }

    const std::size_t plus = text.find('+', colon + 1);
    const char* const beat_end =
        plus == std::string::npos ? text.data() + text.size() : text.data() + plus;
    const auto beat = parseWholeInt(text.data() + colon + 1, beat_end);
    if (!beat.has_value() || *beat <= 0)
    {
        return std::nullopt;
    }

    Fraction offset{};
    if (plus != std::string::npos)
    {
        const auto fraction = parseBeatFractionToken(text.substr(plus + 1));
        // The fractional extension must address a point strictly inside the beat; a whole-beat
        // remainder belongs in the beat number itself.
        if (!fraction.has_value() || fraction->numerator <= 0 || *fraction >= Fraction{1})
        {
            return std::nullopt;
        }
        offset = *fraction;
    }

    return GridPosition{.measure = *measure, .beat = *beat, .offset = offset};
}

std::string formatGridPositionToken(const GridPosition& position)
{
    std::string text = std::to_string(position.measure) + ":" + std::to_string(position.beat);
    if (position.offset.numerator != 0)
    {
        text += "+" + formatBeatFractionToken(position.offset);
    }
    return text;
}

std::optional<Fraction> parseBeatFractionToken(const std::string& text)
{
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos)
    {
        const auto whole = parseWholeInt(text.data(), text.data() + text.size());
        if (!whole.has_value())
        {
            return std::nullopt;
        }
        return Fraction{*whole};
    }

    const auto numerator = parseWholeInt(text.data(), text.data() + slash);
    const auto denominator = parseWholeInt(text.data() + slash + 1, text.data() + text.size());
    if (!numerator.has_value() || !denominator.has_value() || *denominator <= 0)
    {
        return std::nullopt;
    }
    return Fraction{*numerator, *denominator};
}

std::string formatBeatFractionToken(Fraction value)
{
    if (value.denominator == 1)
    {
        return std::to_string(value.numerator);
    }
    return std::to_string(value.numerator) + "/" + std::to_string(value.denominator);
}

} // namespace rock_hero::common::core
