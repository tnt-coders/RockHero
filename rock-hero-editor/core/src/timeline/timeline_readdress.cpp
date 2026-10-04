#include "timeline/timeline_readdress.h"

#include "timeline/tempo_map_edits.h"

#include <cassert>
#include <format>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/timeline/time_signature_text.h>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// A place as a token under the new meters, rounded onto the lattice only where it lies off it.
// Exact rationals both ways, so the lattice place survives whole, and the assert that says so
// costs one more address query.
[[nodiscard]] common::core::GridPosition addressOf(
    const common::core::TempoMap& to, const common::core::Fraction whole_note)
{
    const common::core::Fraction place = common::core::nearestTickPlace(whole_note);
    const common::core::GridPosition position =
        common::core::advanceGridPositionByWholeNotes(to, common::core::GridPosition{}, place);
    assert(
        to.wholeNotePositionAt(position) == place &&
        "addressing keeps the whole-note place exactly");
    return position;
}

// Where a refused meter stands now, under the meters being replaced: the old meter the content map
// carries onto its place, or, for a meter the edit itself authors (only the identity places one),
// that place.
[[nodiscard]] common::core::Fraction placeNow(
    const common::core::TempoMap& old_map, const ContentMap& content,
    const common::core::Fraction place)
{
    for (const PlacedMeter& old_meter : placedMetersOf(old_map))
    {
        if (content(old_meter.whole_note) == place)
        {
            return old_meter.whole_note;
        }
    }
    return place;
}

// Walks the places in order. A reign is a whole number of its own measures exactly when the next
// place is a downbeat under the meters before it, and that count is what numbers the next meter.
[[nodiscard]] std::expected<std::vector<common::core::TimeSignatureChange>, ReaddressError>
timeSignaturesOf(
    const common::core::TempoMap& old_map, std::span<const PlacedMeter> meters,
    const ContentMap& content)
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
                // Named where the charter sees it now.
                return std::unexpected{ReaddressError{
                    .code = ReaddressErrorCode::MeterOffDownbeat,
                    .message = std::format(
                        "the {} time signature at {} would start mid-measure under the {} before "
                        "it",
                        timeSignatureText(placed.meter),
                        common::core::formatGridPositionToken(
                            common::core::advanceGridPositionByWholeNotes(
                                old_map,
                                common::core::GridPosition{},
                                placeNow(old_map, content, placed.whole_note))),
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

std::vector<PlacedAnchor> placedAnchorsOf(const common::core::TempoMap& tempo_map)
{
    std::vector<PlacedAnchor> anchors;
    anchors.reserve(tempo_map.anchors().size());
    for (const common::core::BeatAnchor& anchor : tempo_map.anchors())
    {
        anchors.push_back(
            PlacedAnchor{
                .whole_note = tempo_map.wholeNotePositionAt(anchor.position),
                .milliseconds = anchor.milliseconds,
            });
    }
    return anchors;
}

common::core::Fraction keptPlace(const common::core::Fraction whole_note)
{
    assert(common::core::isOnTickLattice(whole_note) && "a kept place is never rounded");
    return whole_note;
}

common::core::GridPosition readdressedPosition(
    const common::core::TempoMap& from, const common::core::TempoMap& to, const ContentMap& content,
    const common::core::GridPosition position)
{
    return addressOf(to, content(from.wholeNotePositionAt(position)));
}

std::expected<common::core::SongTimeline, ReaddressError> readdressTimeline(
    const common::core::SongTimeline& timeline, std::span<const PlacedMeter> meters,
    std::span<const PlacedAnchor> anchors, const ContentMap& content)
{
    const common::core::TempoMap& old_map = timeline.tempo_map;
    std::expected<std::vector<common::core::TimeSignatureChange>, ReaddressError> signatures =
        timeSignaturesOf(old_map, meters, content);
    if (!signatures.has_value())
    {
        return std::unexpected{std::move(signatures.error())};
    }

    // The new meters under the old anchors: wrong in time, right in address, so it is asked only
    // address questions, which the meters alone answer.
    const common::core::TempoMap address_map{*signatures, old_map.anchors()};
    const auto mapped =
        [&old_map, &address_map, &content](const common::core::GridPosition position) {
            return readdressedPosition(old_map, address_map, content, position);
        };

    // A ring or a keyframe offset counts BEATS from its onset, and a beat is one denominator
    // note, so it is re-expressed: the new beat distance from the mapped onset to the place it
    // reached under the old map, mapped.
    const auto reexpress = [&address_map, &mapped](
                               const common::core::GridPosition onset,
                               const common::core::GridPosition old_end) {
        return common::core::beatDistance(address_map, onset, mapped(old_end));
    };

    std::vector<common::core::BeatAnchor> placed_anchors;
    placed_anchors.reserve(anchors.size());
    for (const PlacedAnchor& anchor : anchors)
    {
        placed_anchors.push_back(
            common::core::BeatAnchor{
                .position = addressOf(address_map, anchor.whole_note),
                .milliseconds = anchor.milliseconds,
            });
    }
    common::core::SongTimeline result{
        .tempo_map = reseatTerminalAnchor(
            common::core::TempoMap{std::move(*signatures), std::move(placed_anchors)}),
        .sections = timeline.sections,
        .arrangements = timeline.arrangements,
    };

    // A section is navigation and moves no sound, so one left mid-measure snaps back to its
    // measure's downbeat (65-Q4). Two snapping onto one downbeat are left for the section rules to
    // refuse, as they refuse any repeated position.
    for (common::core::SongSection& section : result.sections)
    {
        section.position = common::core::measureDownbeat(mapped(section.position));
    }

    for (common::core::ArrangementTimeline& arrangement : result.arrangements)
    {
        if (arrangement.chart.has_value())
        {
            common::core::Chart& chart = *arrangement.chart;
            for (common::core::ChartNote& note : chart.notes)
            {
                // Every duration is measured under the old map from the old onset, so the onset is
                // written last.
                const common::core::GridPosition onset = mapped(note.position);
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
                placement.position = mapped(placement.position);
            }
        }
        for (common::core::ToneRegion& region : arrangement.tone_track.regions)
        {
            region.start = mapped(region.start);
        }
        for (common::core::ToneParameterAutomation& lane : arrangement.tone_automation)
        {
            for (common::core::ToneAutomationPoint& point : lane.points)
            {
                point.position = mapped(point.position);
            }
        }
    }
    return result;
}

} // namespace rock_hero::editor::core
