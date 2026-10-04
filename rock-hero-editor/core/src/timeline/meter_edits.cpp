#include "timeline/meter_edits.h"

#include "timeline/tempo_map_edits.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/timeline/time_signature_text.h>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Walks the places in order. A reign is a whole number of its own measures exactly when the next
// place is a downbeat under the meters before it, and that count is what numbers the next meter.
[[nodiscard]] std::expected<std::vector<common::core::TimeSignatureChange>, MeterEditError>
timeSignaturesOf(const common::core::TempoMap& tempo_map, std::span<const PlacedMeter> meters)
{
    assert(
        (meters.empty() || meters.front().whole_note == common::core::Fraction{}) &&
        "the first meter governs from the song's start");
    std::vector<common::core::TimeSignatureChange> signatures;
    signatures.reserve(meters.size());
    const PlacedMeter* previous = nullptr;
    for (const PlacedMeter& placed : meters)
    {
        int measure = 1;
        if (previous != nullptr)
        {
            // The reign over the length of one of its measures.
            const common::core::Fraction measure_length = previous->meter.measureWholeNotes();
            const common::core::Fraction reign_measures =
                (placed.whole_note - previous->whole_note) *
                common::core::Fraction{measure_length.denominator, measure_length.numerator};
            if (reign_measures.denominator != 1)
            {
                // Named where the charter sees it now: its place under the meters being replaced.
                return std::unexpected{MeterEditError{
                    .code = MeterEditErrorCode::MeterOffDownbeat,
                    .message = std::format(
                        "the {} time signature at {} would start mid-measure under the {} before "
                        "it",
                        timeSignatureText(placed.meter),
                        common::core::formatGridPositionToken(
                            common::core::advanceGridPositionByWholeNotes(
                                tempo_map, common::core::GridPosition{}, placed.whole_note)),
                        timeSignatureText(previous->meter)),
                }};
            }
            measure = signatures.back().measure + reign_measures.numerator;
        }
        signatures.push_back(
            common::core::TimeSignatureChange{.measure = measure, .meter = placed.meter});
        previous = &placed;
    }
    return signatures;
}

} // namespace

std::vector<PlacedMeter> placedMetersOf(const common::core::TempoMap& tempo_map)
{
    std::vector<PlacedMeter> meters;
    meters.reserve(tempo_map.timeSignatures().size());
    for (const common::core::TimeSignatureChange& signature : tempo_map.timeSignatures())
    {
        meters.push_back(
            PlacedMeter{
                .whole_note =
                    tempo_map.wholeNotePositionAt(common::core::measureDownbeat(signature.measure)),
                .meter = signature.meter,
            });
    }
    return meters;
}

// Both legs are exact rationals, so nothing rounds, and the assert that says so costs one more
// address query.
common::core::GridPosition readdressedPosition(
    const common::core::TempoMap& from, const common::core::TempoMap& to,
    const common::core::GridPosition position)
{
    const common::core::Fraction whole_note = from.wholeNotePositionAt(position);
    const common::core::GridPosition readdressed =
        common::core::advanceGridPositionByWholeNotes(to, common::core::GridPosition{}, whole_note);
    assert(
        to.wholeNotePositionAt(readdressed) == whole_note &&
        "re-addressing keeps the whole-note position exactly");
    return readdressed;
}

// One address query per measure on each side, once per command: a song's measures, not its notes.
std::optional<int> firstRenumberedMeasure(
    const common::core::TempoMap& before, const common::core::TempoMap& after)
{
    const int last = std::max(
        common::core::terminalGridPosition(before).measure,
        common::core::terminalGridPosition(after).measure);
    for (int measure = 1; measure <= last; ++measure)
    {
        const common::core::GridPosition downbeat = common::core::measureDownbeat(measure);
        if (before.wholeNotePositionAt(downbeat) != after.wholeNotePositionAt(downbeat))
        {
            return measure;
        }
    }
    return std::nullopt;
}

std::expected<common::core::SongTimeline, MeterEditError> readdressUnderMeters(
    const common::core::SongTimeline& timeline, std::span<const PlacedMeter> meters)
{
    const common::core::TempoMap& old_map = timeline.tempo_map;
    std::expected<std::vector<common::core::TimeSignatureChange>, MeterEditError> signatures =
        timeSignaturesOf(old_map, meters);
    if (!signatures.has_value())
    {
        return std::unexpected{std::move(signatures.error())};
    }

    // The old anchor TOKENS read under the new meters: wrong in time, right in address, so it is
    // asked only address questions.
    const common::core::TempoMap address_map{*signatures, old_map.anchors()};
    const auto readdress = [&old_map, &address_map](const common::core::GridPosition position) {
        return readdressedPosition(old_map, address_map, position);
    };

    // A ring or a keyframe offset counts BEATS from its onset, and a beat is one denominator
    // note, so it is re-expressed: the new beat distance from the re-addressed onset to the place
    // it reached under the old meters, re-addressed.
    const auto reexpress = [&address_map, &readdress](
                               const common::core::GridPosition onset,
                               const common::core::GridPosition old_end) {
        return common::core::beatDistance(address_map, onset, readdress(old_end));
    };

    std::vector<common::core::BeatAnchor> anchors = old_map.anchors();
    for (common::core::BeatAnchor& anchor : anchors)
    {
        anchor.position = readdress(anchor.position);
    }
    common::core::SongTimeline result{
        .tempo_map = reseatTerminalAnchor(
            common::core::TempoMap{std::move(*signatures), std::move(anchors)}),
        .sections = timeline.sections,
        .arrangements = timeline.arrangements,
    };

    // A section is navigation and moves no sound, so one left mid-measure snaps back to its
    // measure's downbeat (65-Q4). Two snapping onto one downbeat are left for the section rules to
    // refuse, as they refuse any repeated position.
    for (common::core::SongSection& section : result.sections)
    {
        section.position = common::core::measureDownbeat(readdress(section.position));
    }

    for (common::core::ArrangementTimeline& arrangement : result.arrangements)
    {
        if (arrangement.chart.has_value())
        {
            common::core::Chart& chart = *arrangement.chart;
            for (common::core::ChartNote& note : chart.notes)
            {
                // Every duration is measured under the old meters from the old onset, so the onset
                // is written last.
                const common::core::GridPosition onset = readdress(note.position);
                for (common::core::Keyframe& keyframe : note.keyframes)
                {
                    keyframe.offset = reexpress(
                        onset,
                        common::core::advanceGridPosition(old_map, note.position, keyframe.offset));
                }
                note.sustain = reexpress(onset, common::core::sustainEndPosition(old_map, note));
                note.position = onset;
            }
            for (common::core::FretHandPosition& placement : chart.fret_hand_positions)
            {
                placement.position = readdress(placement.position);
            }
        }
        for (common::core::ToneRegion& region : arrangement.tone_track.regions)
        {
            region.start = readdress(region.start);
        }
        for (common::core::ToneParameterAutomation& lane : arrangement.tone_automation)
        {
            for (common::core::ToneAutomationPoint& point : lane.points)
            {
                point.position = readdress(point.position);
            }
        }
    }
    return result;
}

} // namespace rock_hero::editor::core
