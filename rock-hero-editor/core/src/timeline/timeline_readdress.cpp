#include "timeline/timeline_readdress.h"

#include <cassert>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
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

// THE path every slice takes: its whole-note place under `from`, sent by the content map, read as a
// token under `to`. Only `to`'s meters are read, so a map whose anchors are still the old ones
// addresses alike.
[[nodiscard]] common::core::GridPosition readdressedPosition(
    const common::core::TempoMap& from, const common::core::TempoMap& to, const ContentMap& content,
    const common::core::GridPosition position)
{
    return addressOf(to, mappedPlace(content, from.wholeNotePositionAt(position)));
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

common::core::SongTimeline readdressTimeline(
    const common::core::SongTimeline& timeline,
    const std::span<const common::core::TimeSignatureChange> meters,
    const std::span<const PlacedAnchor> anchors, const ContentMap& content)
{
    assert(
        (meters.empty() || meters.front().place == common::core::Fraction{}) &&
        "the first meter governs from the song's start");
    const common::core::TempoMap& old_map = timeline.tempo_map;
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
        .tempo_map = common::core::TempoMap{std::move(signatures), std::move(placed_anchors)},
        .sections = timeline.sections,
        .arrangements = timeline.arrangements,
    };

    for (common::core::SongSection& section : result.sections)
    {
        section.position = mapped(section.position);
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
