#include "timeline/tempo_scale.h"

#include "shared/ratio_text.h"

#include <cassert>
#include <format>
#include <optional>
#include <vector>

namespace rock_hero::editor::core
{

// A meter's "6/8" is not its "3/4", but a factor's is, so this one reduces before it is bounded.
std::expected<common::core::Fraction, std::string> parseScaleFactor(const std::string_view text)
{
    std::optional<common::core::Fraction> factor;
    if (text.find('/') == std::string_view::npos)
    {
        if (const std::optional<int> whole = parsePositiveWhole(text); whole.has_value())
        {
            factor = common::core::Fraction{*whole};
        }
    }
    else if (const std::optional<TypedRatio> ratio = parseTypedRatio(text); ratio.has_value())
    {
        factor = common::core::Fraction{ratio->numerator, ratio->denominator};
    }
    if (!factor.has_value())
    {
        return std::unexpected{std::format("not a scale factor: {}", text)};
    }
    const common::core::Fraction reduced = *factor;
    if (reduced.numerator > g_max_scale_factor_term ||
        reduced.denominator > g_max_scale_factor_term)
    {
        return std::unexpected{std::format(
            "a scale factor's terms must each be at most {}: {}", g_max_scale_factor_term, text)};
    }
    return reduced;
}

ContentMap scaledPlaces(const common::core::Fraction factor)
{
    return [factor](const common::core::Fraction whole_note) { return whole_note * factor; };
}

// The map is scaled exactly as the content is, so a span keeps its music and its milliseconds, and
// only where the scaled content lands between two ticks does the transform round it.
std::expected<common::core::SongTimeline, ReaddressError> scaledTimeline(
    const common::core::SongTimeline& timeline, const common::core::Fraction factor)
{
    assert(factor > common::core::Fraction{} && "a scale factor is positive");
    const ContentMap scaled = scaledPlaces(factor);
    std::vector<common::core::TimeSignatureChange> meters = timeline.tempo_map.timeSignatures();
    for (common::core::TimeSignatureChange& meter : meters)
    {
        meter.place = scaled(meter.place);
    }
    std::vector<PlacedAnchor> anchors = placedAnchorsOf(timeline.tempo_map);
    for (PlacedAnchor& anchor : anchors)
    {
        anchor.whole_note = scaled(anchor.whole_note);
    }
    return readdressTimeline(timeline, meters, anchors, scaled);
}

} // namespace rock_hero::editor::core
