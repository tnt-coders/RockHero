#include "timeline/tempo_map_edits.h"

#include "timeline/tempo_map_snapshot.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// A refusal that names no record: nothing but the anchor the edit addressed stood in its way, so it
// lights nothing beside the caller's subject.
[[nodiscard]] std::unexpected<TempoMapEditError> refuse(
    TempoMapEditErrorCode code, std::string message)
{
    return std::unexpected{
        TempoMapEditError{.code = code, .message = std::move(message), .lit = {}}
    };
}

// Index of the anchor standing exactly at the position, if any — the terminal included, which the
// move and delete verbs address (a span lookup is TempoMap::spanStartingAt).
[[nodiscard]] std::optional<std::size_t> anchorIndexAt(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    const auto found = std::ranges::find(anchors, position, &common::core::BeatAnchor::position);
    if (found == anchors.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(anchors.begin(), found));
}

// Builds the edited map and asks the shared rules of it, so every verb refuses exactly what the
// package reader would and states no ordering or grid rule of its own; a broken rule lights the
// records it names on the source map, the one on screen.
[[nodiscard]] TempoMapEditResult validated(
    const common::core::TempoMap& source, std::vector<common::core::BeatAnchor> anchors)
{
    common::core::TempoMap edited{source.timeSignatures(), std::move(anchors)};
    if (const auto rules = common::core::validateTempoMapRules(edited); !rules.has_value())
    {
        return std::unexpected{TempoMapEditError{
            .code = TempoMapEditErrorCode::BrokenRule,
            .message = rules.error().message,
            .lit = tempoMapCulprits(rules.error(), source),
        }};
    }
    return edited;
}

// The tempo a span holds, as seconds per quarter note: its stored time over its quarter length.
[[nodiscard]] double spanSecondsPerQuarter(
    const common::core::TempoMap& tempo_map, const std::size_t span)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    return (anchors[span + 1].seconds() - anchors[span].seconds()) /
           tempo_map.spanQuarterLength(span);
}

// A non-negative distance in whole milliseconds divided by a factor, halves rounding up: exact
// integer arithmetic, since a time between two milliseconds cannot be stored anyway. The quotient
// is taken before the denominator multiplies, so the only products are the result itself and the
// remainder's, which the factor's small terms bound.
[[nodiscard]] std::int64_t scaledDistance(
    const std::int64_t distance, const common::core::Fraction factor)
{
    const std::int64_t whole = distance / factor.numerator;
    const std::int64_t part = distance % factor.numerator;
    return whole * factor.denominator + (2 * part * factor.denominator + factor.numerator) /
                                            (2 * static_cast<std::int64_t>(factor.numerator));
}

} // namespace

TempoMapEditResult insertTempoAnchor(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition position)
{
    // No range check of its own: an off-lattice position, an occupied one and one at or past the
    // terminal (whose time the map clamps to the terminal's) all break a rule.
    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    const auto after =
        std::ranges::find_if(anchors, [&position](const common::core::BeatAnchor& anchor) {
            return anchor.position > position;
        });
    anchors.insert(
        after,
        common::core::BeatAnchor{
            .position = position,
            .milliseconds = common::core::nearestAnchorMilliseconds(tempo_map.secondsAt(position)),
        });
    return validated(tempo_map, std::move(anchors));
}

TempoMapEditResult moveTempoAnchor(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition position,
    const std::int64_t milliseconds)
{
    const std::optional<std::size_t> index = anchorIndexAt(tempo_map, position);
    if (!index.has_value())
    {
        return refuse(
            TempoMapEditErrorCode::NoAnchorAtPosition, "no tempo anchor stands at that position");
    }

    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    anchors[*index].milliseconds = milliseconds;
    return validated(tempo_map, std::move(anchors));
}

TempoMapEditResult setTempoSpanBpm(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition position,
    const double quarter_bpm, const double cover_until_seconds)
{
    if (!std::isfinite(quarter_bpm) || quarter_bpm <= 0.0)
    {
        return refuse(TempoMapEditErrorCode::InvalidBpm, "a tempo must be a positive number");
    }

    const std::optional<std::size_t> found = tempo_map.spanStartingAt(position);
    if (!found.has_value())
    {
        return refuse(
            TempoMapEditErrorCode::NoAnchorAtPosition, "no tempo span starts at that position");
    }
    const std::size_t span = *found;

    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    // A typed tempo slow enough can put the span's end past any time an anchor can hold.
    const double span_end_seconds =
        anchors[span].seconds() + tempo_map.spanQuarterLength(span) * 60.0 / quarter_bpm;
    if (!common::core::isRepresentableAnchorTime(span_end_seconds))
    {
        return refuse(TempoMapEditErrorCode::BrokenRule, "anchor time is out of range");
    }
    const std::int64_t span_end = common::core::nearestAnchorMilliseconds(span_end_seconds);
    // A whole-millisecond shift, so every later span keeps its length exactly.
    const std::int64_t shift = span_end - anchors[span + 1].milliseconds;
    for (std::size_t later = span + 1; later < anchors.size(); ++later)
    {
        anchors[later].milliseconds += shift;
    }
    if (span_end <= anchors[span].milliseconds)
    {
        return validated(tempo_map, std::move(anchors)); // A tempo too fast for the grid: refused.
    }

    // The last span keeps the typed tempo exactly when it is the span typed, and its stored tempo
    // otherwise; either way the terminal grows from it if the ripple left the audio uncovered.
    const std::size_t last_span = anchors.size() - 2;
    const common::core::TempoMap rippled{tempo_map.timeSignatures(), anchors};
    const double seconds_per_quarter =
        span == last_span ? 60.0 / quarter_bpm : spanSecondsPerQuarter(rippled, last_span);
    anchors.back() = rippled.terminalCovering(cover_until_seconds, seconds_per_quarter);
    return validated(tempo_map, std::move(anchors));
}

TempoMapEditResult scaleTempoMap(
    const common::core::TempoMap& tempo_map, const common::core::Fraction factor,
    const double cover_until_seconds)
{
    assert(factor > common::core::Fraction{} && "a scale factor is positive");
    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    const std::int64_t origin = anchors.front().milliseconds;
    // A factor small enough can put the terminal past any time an anchor can hold.
    const double terminal_seconds =
        anchors.front().seconds() +
        (anchors.back().seconds() - anchors.front().seconds()) / factor.toDouble();
    if (!common::core::isRepresentableAnchorTime(terminal_seconds))
    {
        return refuse(TempoMapEditErrorCode::BrokenRule, "anchor time is out of range");
    }
    for (common::core::BeatAnchor& anchor : anchors)
    {
        anchor.milliseconds = origin + scaledDistance(anchor.milliseconds - origin, factor);
    }

    // The terminal grows from the last span's scaled tempo if a faster map left the audio
    // uncovered. Two anchors scaled onto one millisecond leave the span between them no tempo, and
    // the rules refuse them whatever the terminal did.
    const std::size_t last_span = anchors.size() - 2;
    const common::core::TempoMap scaled{tempo_map.timeSignatures(), anchors};
    anchors.back() =
        scaled.terminalCovering(cover_until_seconds, spanSecondsPerQuarter(scaled, last_span));
    return validated(tempo_map, std::move(anchors));
}

TempoMapEditResult deleteTempoAnchor(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition position)
{
    const std::optional<std::size_t> index = anchorIndexAt(tempo_map, position);
    if (!index.has_value())
    {
        return refuse(
            TempoMapEditErrorCode::NoAnchorAtPosition, "no tempo anchor stands at that position");
    }
    if (*index == 0 || *index + 1 == tempo_map.anchors().size())
    {
        return refuse(
            TempoMapEditErrorCode::ProtectedAnchor,
            "the first and the terminal tempo anchors cannot be deleted");
    }

    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    anchors.erase(anchors.begin() + static_cast<std::ptrdiff_t>(*index));
    return validated(tempo_map, std::move(anchors));
}

std::int64_t tempoAnchorNudgeMilliseconds(double pixels_per_second) noexcept
{
    // An unreported zoom, or one so far out that a pixel spans no storable time, nudges by the
    // finest step an anchor can take.
    if (!(pixels_per_second > 0.0) ||
        !common::core::isRepresentableAnchorTime(1.0 / pixels_per_second))
    {
        return 1;
    }
    return std::max<std::int64_t>(
        1, common::core::nearestAnchorMilliseconds(1.0 / pixels_per_second));
}

} // namespace rock_hero::editor::core
