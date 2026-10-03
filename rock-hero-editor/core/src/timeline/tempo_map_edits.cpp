#include "timeline/tempo_map_edits.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] std::unexpected<TempoMapEditError> refuse(
    TempoMapEditErrorCode code, std::string message)
{
    return std::unexpected{TempoMapEditError{.code = code, .message = std::move(message)}};
}

// Index of the anchor standing exactly at the beat, if any.
[[nodiscard]] std::optional<std::size_t> anchorIndexAt(
    const common::core::TempoMap& tempo_map, int measure, int beat)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    const auto found =
        std::ranges::find_if(anchors, [measure, beat](const common::core::BeatAnchor& anchor) {
            return anchor.measure == measure && anchor.beat == beat;
        });
    if (found == anchors.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(anchors.begin(), found));
}

// Builds the edited map and asks the shared rules of it, so every verb refuses exactly what the
// package reader would and states no ordering or grid rule of its own.
[[nodiscard]] TempoMapEditResult validated(
    const common::core::TempoMap& source, std::vector<common::core::BeatAnchor> anchors)
{
    common::core::TempoMap edited{source.timeSignatures(), std::move(anchors)};
    if (const auto rules = common::core::validateTempoMapRules(edited); !rules.has_value())
    {
        return refuse(TempoMapEditErrorCode::BrokenRule, rules.error().message);
    }
    return edited;
}

} // namespace

TempoMapEditResult insertTempoAnchor(const common::core::TempoMap& tempo_map, int measure, int beat)
{
    // No range check of its own: a beat outside its measure, an occupied beat and a beat at or
    // past the terminal (whose time the map clamps to the terminal's) all break the ordering rule.
    const std::int64_t index = tempo_map.globalBeatIndex(measure, beat);
    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    const auto after =
        std::ranges::find_if(anchors, [&tempo_map, index](const common::core::BeatAnchor& anchor) {
            return tempo_map.globalBeatIndex(anchor.measure, anchor.beat) > index;
        });
    anchors.insert(
        after,
        common::core::BeatAnchor{
            .measure = measure,
            .beat = beat,
            .seconds = common::core::nearestAnchorSeconds(tempo_map.secondsAtBeat(measure, beat)),
        });
    return validated(tempo_map, std::move(anchors));
}

TempoMapEditResult moveTempoAnchor(
    const common::core::TempoMap& tempo_map, int measure, int beat, double seconds)
{
    const std::optional<std::size_t> index = anchorIndexAt(tempo_map, measure, beat);
    if (!index.has_value())
    {
        return refuse(TempoMapEditErrorCode::NoAnchorAtBeat, "no tempo anchor stands at that beat");
    }

    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    anchors[*index].seconds = common::core::nearestAnchorSeconds(seconds);
    return validated(tempo_map, std::move(anchors));
}

TempoMapEditResult setTempoSpanBpm(
    const common::core::TempoMap& tempo_map, int measure, int beat, double quarter_bpm,
    double cover_until_seconds)
{
    if (!std::isfinite(quarter_bpm) || quarter_bpm <= 0.0)
    {
        return refuse(TempoMapEditErrorCode::InvalidBpm, "a tempo must be a positive number");
    }

    const std::optional<std::size_t> index = anchorIndexAt(tempo_map, measure, beat);
    if (!index.has_value() || *index + 1 >= tempo_map.anchors().size())
    {
        return refuse(TempoMapEditErrorCode::NoAnchorAtBeat, "no tempo span starts at that beat");
    }
    const std::size_t span = *index;

    std::vector<common::core::BeatAnchor> anchors = tempo_map.anchors();
    const double span_end = common::core::nearestAnchorSeconds(
        anchors[span].seconds + tempo_map.spanQuarterLength(span) * 60.0 / quarter_bpm);
    const double shift = span_end - anchors[span + 1].seconds;
    for (std::size_t later = span + 1; later < anchors.size(); ++later)
    {
        anchors[later].seconds = common::core::nearestAnchorSeconds(anchors[later].seconds + shift);
    }
    if (span_end <= anchors[span].seconds)
    {
        return validated(tempo_map, std::move(anchors)); // A tempo too fast for the grid: refused.
    }

    // The last span keeps the typed tempo exactly when it is the span typed, and its stored tempo
    // otherwise; either way the terminal grows from it if the ripple left the audio uncovered.
    const std::size_t last_span = anchors.size() - 2;
    const common::core::TempoMap rippled{tempo_map.timeSignatures(), anchors};
    const double seconds_per_quarter = span == last_span
                                           ? 60.0 / quarter_bpm
                                           : (anchors.back().seconds - anchors[last_span].seconds) /
                                                 rippled.spanQuarterLength(last_span);
    anchors.back() = rippled.terminalCovering(cover_until_seconds, seconds_per_quarter);
    return validated(tempo_map, std::move(anchors));
}

TempoMapEditResult deleteTempoAnchor(const common::core::TempoMap& tempo_map, int measure, int beat)
{
    const std::optional<std::size_t> index = anchorIndexAt(tempo_map, measure, beat);
    if (!index.has_value())
    {
        return refuse(TempoMapEditErrorCode::NoAnchorAtBeat, "no tempo anchor stands at that beat");
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

double tempoAnchorNudgeSeconds(double pixels_per_second) noexcept
{
    if (!std::isfinite(pixels_per_second) || pixels_per_second <= 0.0)
    {
        return common::core::g_anchor_seconds_step;
    }
    return std::max(
        common::core::g_anchor_seconds_step,
        common::core::nearestAnchorSeconds(1.0 / pixels_per_second));
}

} // namespace rock_hero::editor::core
