#include <charconv>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>

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
        const auto fraction = parseFractionToken(text.substr(plus + 1));
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
        text += "+" + formatFractionToken(position.offset);
    }
    return text;
}

std::string formatChartPlaceToken(const GridPosition& position, const int string)
{
    return formatGridPositionToken(position) + " string " + std::to_string(string);
}

std::string formatHandPositionToken(const GridPosition& position)
{
    return "hand position " + formatGridPositionToken(position);
}

std::string formatShapeHoldToken(const ShapeHold& hold)
{
    return "shape hold " + formatGridPositionToken(hold.start) + " to " +
           formatGridPositionToken(hold.end);
}

// Reads the three shapes leniently, then holds the text to the formatter's spelling of the value
// it read: the formatter is the one statement of the grammar, and a spelling that is not its own
// (an improper or unreduced fraction, a written zero part) is refused by that fixpoint rather than
// by a second statement of the rules.
std::optional<Fraction> parseFractionToken(const std::string& text)
{
    const char* const end = text.data() + text.size();
    const std::size_t plus = text.find('+');
    const std::size_t slash = text.find('/');
    std::optional<Fraction> value;
    if (plus == std::string::npos && slash == std::string::npos)
    {
        if (const auto whole = parseWholeInt(text.data(), end); whole.has_value())
        {
            value = Fraction{*whole};
        }
    }
    else if (slash != std::string::npos && (plus == std::string::npos || plus < slash))
    {
        const bool mixed = plus != std::string::npos;
        const char* const fraction_begin = mixed ? text.data() + plus + 1 : text.data();
        const std::optional<int> whole =
            mixed ? parseWholeInt(text.data(), text.data() + plus) : std::optional{0};
        const auto numerator = parseWholeInt(fraction_begin, text.data() + slash);
        const auto denominator = parseWholeInt(text.data() + slash + 1, end);
        if (whole.has_value() && numerator.has_value() && denominator.has_value() &&
            *denominator > 0)
        {
            value = Fraction{*whole} + Fraction{*numerator, *denominator};
        }
    }
    if (!value.has_value() || formatFractionToken(*value) != text)
    {
        return std::nullopt;
    }
    return value;
}

// The lattice is asked here, at admission, because everything downstream of a reader — the
// normalizer before the rules included — measures from lattice places; a place between two ticks
// must never become a position.
std::optional<GridPosition> parsePlaceToken(const TempoMap& tempo_map, const std::string& text)
{
    const std::optional<Fraction> place = parseFractionToken(text);
    if (!place.has_value() || !isOnTickLattice(*place))
    {
        return std::nullopt;
    }
    return tempo_map.gridPositionAtWholeNote(*place);
}

std::string formatPlaceToken(const TempoMap& tempo_map, const GridPosition& position)
{
    return formatFractionToken(tempo_map.wholeNotePositionAt(position));
}

std::string formatFractionToken(const Fraction value)
{
    const int whole = value.numerator / value.denominator;
    const Fraction fraction = value - Fraction{whole};
    if (fraction.numerator == 0)
    {
        return std::to_string(whole);
    }
    const std::string fraction_text =
        std::to_string(fraction.numerator) + "/" + std::to_string(fraction.denominator);
    return whole == 0 ? fraction_text : std::to_string(whole) + "+" + fraction_text;
}

} // namespace rock_hero::common::core
