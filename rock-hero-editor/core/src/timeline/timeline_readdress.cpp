#include "timeline/timeline_readdress.h"

#include "timeline/tempo_map_edits.h"

#include <cassert>
#include <format>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/timeline/time_signature_text.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Where the content map sends an old place, rounded onto the lattice only where it lands off it.
[[nodiscard]] common::core::Fraction mappedPlace(
    const ContentMap& content, const common::core::Fraction whole_note)
{
    return common::core::nearestTickPlace(content(whole_note));
}

// A lattice place as a token under the new meters. Exact rationals both ways, so the place
// survives whole, and the assert that says so costs one more address query.
[[nodiscard]] common::core::GridPosition addressOf(
    const common::core::TempoMap& to, const common::core::Fraction place)
{
    assert(common::core::isOnTickLattice(place) && "only a lattice place is addressed");
    const common::core::GridPosition position = to.gridPositionAtWholeNote(place);
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
    for (const common::core::TimeSignatureChange& old_meter : old_map.timeSignatures())
    {
        if (content(old_meter.place) == place)
        {
            return old_meter.place;
        }
    }
    return place;
}

// Walks the places in order: a reign is a whole number of its own measures exactly when the next
// place is a downbeat under the meters before it (65-Q3).
[[nodiscard]] std::expected<void, ReaddressError> requireWholeMeasureReigns(
    const common::core::TempoMap& old_map,
    const std::span<const common::core::TimeSignatureChange> meters, const ContentMap& content)
{
    assert(
        (meters.empty() || meters.front().place == common::core::Fraction{}) &&
        "the first meter governs from the song's start");
    for (std::size_t index = 1; index < meters.size(); ++index)
    {
        const common::core::TimeSignatureChange& previous = meters[index - 1];
        const common::core::TimeSignatureChange& placed = meters[index];
        // The reign over the length of one of its measures.
        const common::core::Fraction measure_length = previous.meter.measureWholeNotes();
        const common::core::Fraction reign_measures =
            (placed.place - previous.place) *
            common::core::Fraction{measure_length.denominator, measure_length.numerator};
        if (reign_measures.denominator != 1)
        {
            // Named where the charter sees it now.
            return std::unexpected{ReaddressError{
                .code = ReaddressErrorCode::MeterOffDownbeat,
                .message = std::format(
                    "the {} time signature at {} would start mid-measure under the {} before it",
                    common::core::timeSignatureText(placed.meter),
                    common::core::formatGridPositionToken(
                        old_map.gridPositionAtWholeNote(placeNow(old_map, content, placed.place))),
                    common::core::timeSignatureText(previous.meter)),
            }};
        }
    }
    return {};
}

} // namespace

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
    return addressOf(to, mappedPlace(content, from.wholeNotePositionAt(position)));
}

std::expected<common::core::SongTimeline, ReaddressError> readdressTimeline(
    const common::core::SongTimeline& timeline,
    const std::span<const common::core::TimeSignatureChange> meters,
    const std::span<const PlacedAnchor> anchors, const ContentMap& content)
{
    const common::core::TempoMap& old_map = timeline.tempo_map;
    if (auto whole = requireWholeMeasureReigns(old_map, meters, content); !whole.has_value())
    {
        return std::unexpected{std::move(whole.error())};
    }
    std::vector<common::core::TimeSignatureChange> signatures{meters.begin(), meters.end()};

    // The new meters under the old anchors: wrong in time, right in address, so it is asked only
    // address questions, which the meters alone answer.
    const common::core::TempoMap address_map{signatures, old_map.anchors()};
    const auto mapped =
        [&old_map, &address_map, &content](const common::core::GridPosition position) {
            return readdressedPosition(old_map, address_map, content, position);
        };

    std::vector<common::core::BeatAnchor> placed_anchors;
    placed_anchors.reserve(anchors.size());
    for (const PlacedAnchor& anchor : anchors)
    {
        placed_anchors.push_back(
            common::core::BeatAnchor{
                .position =
                    addressOf(address_map, common::core::nearestTickPlace(anchor.whole_note)),
                .milliseconds = anchor.milliseconds,
            });
    }
    common::core::SongTimeline result{
        .tempo_map = reseatTerminalAnchor(
            common::core::TempoMap{std::move(signatures), std::move(placed_anchors)}),
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
                // A ring and a keyframe offset are note values from the onset, so each becomes the
                // distance between where its two instants are sent — both rounded the way every
                // place is, which keeps the end on the lattice wherever the onset rounds. A meter
                // edit sends every place to itself, so it leaves every duration untouched.
                const common::core::Fraction old_onset = old_map.wholeNotePositionAt(note.position);
                const common::core::Fraction onset = mappedPlace(content, old_onset);
                for (common::core::Keyframe& keyframe : note.keyframes)
                {
                    keyframe.offset = mappedPlace(content, old_onset + keyframe.offset) - onset;
                }
                note.sustain = mappedPlace(content, old_onset + note.sustain) - onset;
                note.position = addressOf(address_map, onset);
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
