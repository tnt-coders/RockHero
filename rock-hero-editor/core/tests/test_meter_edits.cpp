#include "timeline/meter_edits.h"
#include "timeline/tempo_map_edits.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/testing/song_timeline_fixtures.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::BeatAnchor;
using common::core::Chart;
using common::core::Fraction;
using common::core::GridPosition;
using common::core::SongSection;
using common::core::SongTimeline;
using common::core::TempoMap;
using common::core::TimeSignatureChange;
using common::core::ToneAutomationPoint;
using common::core::ToneRegion;
using common::core::testing::anchor;
using common::core::testing::downbeat;
using testing::addressesErased;
using testing::at;
using testing::chartOf;
using testing::Dice;
using testing::forEachSlicePair;
using testing::g_reseated_time_tolerance_seconds;
using testing::g_time_tolerance_seconds;
using testing::makeNote;
using testing::randomMeter;
using testing::randomTimeline;
using testing::regionId;
using testing::requireValid;
using testing::respelled;
using testing::sliceArrangement;
using testing::SliceKind;
using testing::sliceMap;
using testing::SlicePair;
using testing::sliceTimeline;

using Catch::Matchers::WithinAbs;

[[nodiscard]] PlacedMeter placed(Fraction whole_note, int numerator, int denominator)
{
    return PlacedMeter{
        .whole_note = whole_note,
        .meter = {.numerator = numerator, .denominator = denominator},
    };
}
// Re-addresses and requires the edit to land.
[[nodiscard]] SongTimeline landed(
    const SongTimeline& timeline, const std::vector<PlacedMeter>& meters)
{
    const std::expected<SongTimeline, ReaddressError> result =
        readdressUnderMeters(timeline, meters);
    INFO((result.has_value() ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    return *result;
}

// The fixture with its 3/4 respelled as 6/8.
[[nodiscard]] SongTimeline respelledSlices()
{
    const SongTimeline before = sliceTimeline();
    requireValid(before);
    const SongTimeline after =
        landed(before, {placed(Fraction{}, 4, 4), placed(Fraction{2}, 6, 8)});
    requireValid(after);
    return after;
}

// One random meter edit of the kinds the verbs make, and the freer ones a later verb may: a
// respelling (never refused), an inserted respelling of the prevailing meter, a restated last
// meter (its reign runs to the terminal, so only the terminal re-seats), a free restatement and a
// deletion (either may leave a later meter mid-measure).
[[nodiscard]] std::vector<PlacedMeter> randomMeterEdit(Dice& dice, const TempoMap& tempo_map)
{
    std::vector<PlacedMeter> meters = placedMetersOf(tempo_map);
    const auto pick = [&dice, &meters] {
        return static_cast<std::size_t>(dice.roll(0, static_cast<int>(meters.size()) - 1));
    };
    switch (dice.roll(0, 4))
    {
        case 0:
        {
            PlacedMeter& respelt = meters[pick()];
            respelt.meter = respelled(respelt.meter);
            break;
        }
        case 1:
        {
            const int terminal_measure = common::core::terminalGridPosition(tempo_map).measure;
            const int measure = dice.roll(1, terminal_measure - 1);
            const TimeSignatureChange prevailing = tempo_map.timeSignatureAt(measure);
            const PlacedMeter inserted{
                .whole_note = tempo_map.wholeNotePositionAt(downbeat(measure)),
                .meter = respelled(prevailing.meter),
            };
            const auto place = std::ranges::find_if(meters, [&inserted](const PlacedMeter& held) {
                return held.whole_note >= inserted.whole_note;
            });
            if (place != meters.end() && place->whole_note == inserted.whole_note)
            {
                *place = inserted;
            }
            else
            {
                meters.insert(place, inserted);
            }
            break;
        }
        case 2:
        {
            meters.back().meter = randomMeter(dice);
            break;
        }
        case 3:
        {
            meters[pick()].meter = randomMeter(dice);
            break;
        }
        default:
        {
            if (meters.size() > 1)
            {
                meters.erase(meters.begin() + dice.roll(1, static_cast<int>(meters.size()) - 1));
            }
            break;
        }
    }
    return meters;
}

// ---------------------------------------------------------------------------------------------
// The time-preservation property.

// What one landed edit did beyond re-addressing.
struct EditFootprint
{
    bool terminal_reseated{false};
    std::vector<bool> section_snapped;

    // Rings and keyframes whose onset and end lie under different denominators in either map: the
    // durations a meter change sits inside, which must keep their value all the same.
    int crossing_rings{0};
    int crossing_keyframes{0};
};

// Whether two places lie under different denominators in either map.
[[nodiscard]] bool crossesDenominator(
    const TempoMap& old_map, const GridPosition& old_onset, const GridPosition& old_end,
    const TempoMap& new_map, const GridPosition& new_onset, const GridPosition& new_end)
{
    return old_map.timeSignatureAt(old_onset.measure).meter.denominator !=
               old_map.timeSignatureAt(old_end.measure).meter.denominator ||
           new_map.timeSignatureAt(new_onset.measure).meter.denominator !=
               new_map.timeSignatureAt(new_end.measure).meter.denominator;
}

// Checks every slice of a landed edit against the timeline it came from: whole-note places exact,
// times equal, every other field untouched. A snapped section and a re-seated terminal are checked
// against their own rules instead.
[[nodiscard]] EditFootprint checkKeepsTime(
    const SongTimeline& before, const SongTimeline& after, const std::vector<PlacedMeter>& meters)
{
    requireValid(after);
    const TempoMap& old_map = before.tempo_map;
    const TempoMap& new_map = after.tempo_map;
    CHECK(placedMetersOf(new_map) == meters);

    EditFootprint footprint;
    const std::vector<BeatAnchor>& old_anchors = old_map.anchors();
    const std::vector<BeatAnchor>& new_anchors = new_map.anchors();
    REQUIRE(new_anchors.size() == old_anchors.size());
    for (std::size_t index = 0; index + 1 < old_anchors.size(); ++index)
    {
        CHECK(
            new_map.wholeNotePositionAt(new_anchors[index].position) ==
            old_map.wholeNotePositionAt(old_anchors[index].position));
        CHECK(new_anchors[index].milliseconds == old_anchors[index].milliseconds);
    }
    const Fraction old_terminal = old_map.wholeNotePositionAt(old_anchors.back().position);
    const GridPosition new_terminal_position = new_anchors.back().position;
    const Fraction new_terminal = new_map.wholeNotePositionAt(new_terminal_position);
    footprint.terminal_reseated = new_terminal != old_terminal;
    if (footprint.terminal_reseated)
    {
        // The first downbeat at or after the place the terminal kept.
        CHECK(new_terminal > old_terminal);
        CHECK(
            new_map.wholeNotePositionAt(downbeat(new_terminal_position.measure - 1)) <
            old_terminal);
    }
    else
    {
        CHECK(new_anchors.back().milliseconds == old_anchors.back().milliseconds);
    }

    const double tolerance =
        footprint.terminal_reseated ? g_reseated_time_tolerance_seconds : g_time_tolerance_seconds;
    const auto keeps_place = [&old_map, &new_map, tolerance](
                                 const GridPosition& old_position,
                                 const GridPosition& new_position) {
        CHECK(
            new_map.wholeNotePositionAt(new_position) == old_map.wholeNotePositionAt(old_position));
        CHECK_THAT(
            new_map.secondsAt(new_position), WithinAbs(old_map.secondsAt(old_position), tolerance));
    };

    forEachSlicePair(before, after, [&](const SlicePair& pair) {
        if (pair.kind == SliceKind::Section)
        {
            const Fraction old_place = old_map.wholeNotePositionAt(pair.old_position);
            const Fraction new_place = new_map.wholeNotePositionAt(pair.new_position);
            footprint.section_snapped.push_back(new_place != old_place);
            if (new_place == old_place)
            {
                keeps_place(pair.old_position, pair.new_position);
                return;
            }
            // Snapped back to the downbeat of the measure it now falls in.
            CHECK(new_place < old_place);
            CHECK(common::core::measureDownbeat(pair.new_position) == pair.new_position);
            CHECK(old_place < new_map.wholeNotePositionAt(downbeat(pair.new_position.measure + 1)));
            return;
        }
        keeps_place(pair.old_position, pair.new_position);
        if (pair.kind != SliceKind::RingEnd && pair.kind != SliceKind::Keyframe)
        {
            return;
        }
        if (crossesDenominator(
                old_map,
                pair.old_onset,
                pair.old_position,
                new_map,
                pair.new_onset,
                pair.new_position))
        {
            if (pair.kind == SliceKind::RingEnd)
            {
                ++footprint.crossing_rings;
            }
            else
            {
                ++footprint.crossing_keyframes;
            }
        }
    });
    // A ring and a keyframe offset are note values, so a meter edit leaves every one untouched.
    REQUIRE(after.arrangements.size() == before.arrangements.size());
    for (std::size_t arrangement = 0; arrangement < before.arrangements.size(); ++arrangement)
    {
        const Chart& old_chart = chartOf(before, arrangement);
        const Chart& new_chart = chartOf(after, arrangement);
        REQUIRE(new_chart.notes.size() == old_chart.notes.size());
        for (std::size_t index = 0; index < old_chart.notes.size(); ++index)
        {
            CHECK(new_chart.notes[index].sustain == old_chart.notes[index].sustain);
            CHECK(new_chart.notes[index].keyframes == old_chart.notes[index].keyframes);
        }
    }
    // Every field but the re-addressed ones is untouched.
    CHECK(addressesErased(after) == addressesErased(before));
    return footprint;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Properties over random songs.

TEST_CASE("Meter edits keep every object's time over random songs", "[core][meter-edits]")
{
    int landed_count = 0;
    int refused_count = 0;
    int reseated_count = 0;
    int snapped_count = 0;
    int round_trips = 0;
    int crossing_rings = 0;
    int crossing_keyframes = 0;
    for (std::uint32_t seed = 1; seed <= 150; ++seed)
    {
        INFO("seed " << seed);
        Dice dice{seed};
        const SongTimeline before = randomTimeline(dice);
        requireValid(before);
        const std::vector<PlacedMeter> meters = randomMeterEdit(dice, before.tempo_map);

        const std::expected<SongTimeline, ReaddressError> after =
            readdressUnderMeters(before, meters);
        if (!after.has_value())
        {
            ++refused_count;
            continue;
        }
        const SongTimeline& edited = *after;
        const auto section_rules =
            common::core::validateSongSectionRules(edited.sections, edited.tempo_map);
        if (!section_rules.has_value())
        {
            // Two sections snapped onto one downbeat: the verb's section rules refuse it (65-Q4).
            CHECK(
                section_rules.error().code ==
                common::core::SongSectionErrorCode::UnsortedOrRepeatedPositions);
            ++refused_count;
            continue;
        }
        ++landed_count;
        const EditFootprint footprint = checkKeepsTime(before, edited, meters);
        const bool any_snapped =
            std::ranges::find(footprint.section_snapped, true) != footprint.section_snapped.end();
        if (footprint.terminal_reseated)
        {
            ++reseated_count;
        }
        if (any_snapped)
        {
            ++snapped_count;
        }
        crossing_rings += footprint.crossing_rings;
        crossing_keyframes += footprint.crossing_keyframes;

        // A -> B -> A restores every token, but for a snapped section and a re-seated terminal. The
        // old meters' places are downbeats under the old meters, so the return never refuses.
        const std::expected<SongTimeline, ReaddressError> back =
            readdressUnderMeters(edited, placedMetersOf(before.tempo_map));
        REQUIRE(back.has_value());
        ++round_trips;
        const SongTimeline& restored = *back;
        CHECK(restored.arrangements == before.arrangements);
        CHECK(restored.tempo_map.timeSignatures() == before.tempo_map.timeSignatures());
        const std::vector<BeatAnchor>& restored_anchors = restored.tempo_map.anchors();
        const std::vector<BeatAnchor>& original_anchors = before.tempo_map.anchors();
        REQUIRE(restored_anchors.size() == original_anchors.size());
        for (std::size_t index = 0; index + 1 < original_anchors.size(); ++index)
        {
            CHECK(restored_anchors[index] == original_anchors[index]);
        }
        if (!footprint.terminal_reseated)
        {
            CHECK(restored_anchors.back() == original_anchors.back());
        }
        REQUIRE(restored.sections.size() == before.sections.size());
        for (std::size_t index = 0; index < before.sections.size(); ++index)
        {
            if (!footprint.section_snapped[index])
            {
                CHECK(restored.sections[index] == before.sections[index]);
            }
        }
    }
    // The dice must reach every branch, or the property proves less than it claims.
    CHECK(landed_count >= 75);
    CHECK(refused_count > 0);
    CHECK(reseated_count > 0);
    CHECK(snapped_count > 0);
    CHECK(round_trips >= 75);
    CHECK(crossing_rings > 0);
    CHECK(crossing_keyframes > 0);
}

// ---------------------------------------------------------------------------------------------
// One test per slice.

TEST_CASE("Meter edit re-addresses tempo anchors keeping their time", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    // 4:2+1/2 in 3/4 is three eighths into measure 4: beat 4 of 6/8. The others stand where the
    // respelling reaches no address.
    CHECK(
        after.tempo_map.anchors() == std::vector<BeatAnchor>{
                                         anchor("1:1", 0),
                                         anchor("2:2+1/2", 2750),
                                         anchor("4:4", 6250),
                                         anchor("6:1", 8500),
                                     });
    CHECK(
        after.tempo_map.timeSignatures() ==
        std::vector<TimeSignatureChange>{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 3, .meter = {.numerator = 6, .denominator = 8}},
        });
}

TEST_CASE("Meter edit re-addresses note onsets", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].position == at("2:4"));
    // Beat 2 of 3/4 is beat 3 of 6/8.
    CHECK(chart.notes[1].position == at("4:3"));
}

// A ring is a note value, so a meter edit leaves it untouched: the half note from 2:4 that ended
// at 3:2 of the 3/4 ends at 3:3 of the 6/8 the same edit respelled it as, at the very same second.
TEST_CASE("Meter edit leaves a ring crossing a meter change untouched", "[core][meter-edits]")
{
    const SongTimeline before = sliceTimeline();
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].sustain == Fraction{1, 2});
    CHECK(
        common::core::advanceGridPositionByWholeNotes(
            after.tempo_map, chart.notes[0].position, chart.notes[0].sustain) == at("3:3"));
    CHECK_THAT(
        after.tempo_map.secondsAt(
            common::core::advanceGridPositionByWholeNotes(
                after.tempo_map, chart.notes[0].position, chart.notes[0].sustain)),
        Catch::Matchers::WithinULP(before.tempo_map.secondsAt(at("3:2")), 0));
    // A ring inside the respelled reign keeps its value too.
    CHECK(chart.notes[1].sustain == Fraction{1, 8});
}

// A keyframe offset is a note value from the onset, so it stays too: a dotted quarter from 2:4
// stood at 3:1+1/2 of the 3/4 and stands at 3:2 of the 6/8, at the same second.
TEST_CASE("Meter edit leaves a keyframe crossing a meter change untouched", "[core][meter-edits]")
{
    const SongTimeline before = sliceTimeline();
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE_FALSE(chart.notes.empty());
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    CHECK(chart.notes[0].keyframes[0].offset == Fraction{3, 8});
    CHECK(chart.notes[0].keyframes[0].bend == std::optional{1.0});
    const GridPosition instant = common::core::advanceGridPositionByWholeNotes(
        after.tempo_map, chart.notes[0].position, chart.notes[0].keyframes[0].offset);
    CHECK(instant == at("3:2"));
    CHECK_THAT(
        after.tempo_map.secondsAt(instant),
        Catch::Matchers::WithinULP(before.tempo_map.secondsAt(at("3:1+1/2")), 0));
}

TEST_CASE("Meter edit re-addresses fret-hand positions", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.fret_hand_positions.size() == 2);
    CHECK(chart.fret_hand_positions[0].position == at("1:1"));
    CHECK(chart.fret_hand_positions[1].position == at("3:6"));
    CHECK(chart.fret_hand_positions[1].fret == 5);
}

TEST_CASE("Meter edit re-addresses tone region starts", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    REQUIRE_FALSE(after.arrangements.empty());
    const std::vector<ToneRegion>& regions = after.arrangements[0].tone_track.regions;
    REQUIRE(regions.size() == 2);
    CHECK(regions[0].start == at("1:1"));
    CHECK(regions[1].start == at("4:2"));
    CHECK(regions[1].id == regionId(2));
}

TEST_CASE("Meter edit re-addresses tone automation points", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    REQUIRE_FALSE(after.arrangements.empty());
    REQUIRE(after.arrangements[0].tone_automation.size() == 1);
    const std::vector<ToneAutomationPoint>& points =
        after.arrangements[0].tone_automation[0].points;
    REQUIRE(points.size() == 2);
    CHECK(points[0].position == at("1:1"));
    CHECK(points[1].position == at("4:1+2/3"));
}

TEST_CASE("Meter edit re-addresses sections on a downbeat", "[core][meter-edits]")
{
    // A shorter first reign renumbers every later measure: 2 whole notes of 4/4 are 4 of 2/4.
    const SongTimeline before = sliceTimeline();
    const SongTimeline after =
        landed(before, {placed(Fraction{}, 2, 4), placed(Fraction{2}, 3, 4)});
    requireValid(after);
    CHECK(
        after.sections == std::vector<SongSection>{
                              SongSection{.position = at("1:1"), .name = "Intro"},
                              SongSection{.position = at("6:1"), .name = "Bridge"},
                          });
}

TEST_CASE("Meter edit re-addresses every arrangement", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 1);
    REQUIRE(chart.notes.size() == 1);
    // Beat 3 of 3/4 is beat 5 of 6/8.
    CHECK(chart.notes[0].position == at("5:5"));
    REQUIRE(after.arrangements.size() == 2);
    CHECK(after.arrangements[1].id == "rhythm");
    REQUIRE(after.arrangements[1].tone_automation.size() == 1);
    REQUIRE(after.arrangements[1].tone_automation[0].points.size() == 2);
    CHECK(after.arrangements[1].tone_automation[0].points[1].position == at("4:1+2/3"));
}

TEST_CASE("Meter edit renumbers the measures after a changed reign", "[core][meter-edits]")
{
    const SongTimeline before = sliceTimeline();
    const SongTimeline after =
        landed(before, {placed(Fraction{}, 2, 4), placed(Fraction{2}, 3, 4)});
    requireValid(after);
    CHECK(
        after.tempo_map.timeSignatures() ==
        std::vector<TimeSignatureChange>{
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 2, .denominator = 4}},
            TimeSignatureChange{.measure = 5, .meter = {.numerator = 3, .denominator = 4}},
        });
    CHECK(after.tempo_map.anchors().back() == anchor("8:1", 8500));
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].position == at("4:2"));
    CHECK(chart.notes[1].position == at("6:2"));
}

TEST_CASE("Placed meters read a map's signatures at their whole notes", "[core][meter-edits]")
{
    CHECK(
        placedMetersOf(sliceMap()) == std::vector<PlacedMeter>{
                                          placed(Fraction{}, 4, 4),
                                          placed(Fraction{2}, 3, 4),
                                      });
}

// ---------------------------------------------------------------------------------------------
// 65-Q3: later meters keep their time.

TEST_CASE("Meter edit refuses a reign that leaves a later meter mid-measure", "[core][meter-edits]")
{
    // Sixteen beats of 4/4 re-metered to 3/4 are 5 1/3 measures, so the 6/8 at 5:1 would start on
    // beat 2 of a measure.
    const SongTimeline before{
        .tempo_map =
            TempoMap{
                {
                    TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
                    TimeSignatureChange{.measure = 5, .meter = {.numerator = 6, .denominator = 8}},
                },
                {anchor("1:1", 0), anchor("7:1", 11000)},
            },
        .sections = {},
        .arrangements = {},
    };
    requireValid(before);
    const std::expected<SongTimeline, ReaddressError> after = readdressUnderMeters(
        before, std::vector{placed(Fraction{}, 3, 4), placed(Fraction{4}, 6, 8)});
    REQUIRE_FALSE(after.has_value());
    CHECK(after.error().code == ReaddressErrorCode::MeterOffDownbeat);
    CHECK(
        after.error().message ==
        "the 6/8 time signature at 5:1 would start mid-measure under the 3/4 before it");
}

TEST_CASE("Meter edit with no later meter re-seats the terminal", "[core][meter-edits]")
{
    // The same sixteen beats of 4/4 to 3/4 with nothing after them: the terminal, kept at whole
    // note 4, lands on 6:2 and re-seats on 7:1, the last span keeping its 500 ms quarter.
    const SongTimeline before{
        .tempo_map = common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("5:1", 8000)}),
        .sections = {},
        .arrangements = {sliceArrangement("lead", {makeNote(at("4:4"), 1, 5, Fraction{1}, {})})},
    };
    requireValid(before);
    const SongTimeline after = landed(before, {placed(Fraction{}, 3, 4)});
    requireValid(after);
    CHECK(
        after.tempo_map ==
        TempoMap{
            {TimeSignatureChange{.measure = 1, .meter = {.numerator = 3, .denominator = 4}}},
            {anchor("1:1", 0), anchor("7:1", 9000)},
        });
    // Content keeps its time under the re-timed span: 4:4 is whole note 3 3/4, beat 1 of 6.
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes[0].position == at("6:1"));
    CHECK_THAT(after.tempo_map.secondsAt(chart.notes[0].position), WithinAbs(7.5, 1e-9));
}

// ---------------------------------------------------------------------------------------------
// 65-Q4: a section left mid-measure snaps to its measure's downbeat.

TEST_CASE("Meter edit snaps a section left mid-measure to its downbeat", "[core][meter-edits]")
{
    // Verse at whole note 2 falls on 3:3 of 3/4 and moves back to 3:1, whole note 1 1/2.
    const SongTimeline before{
        .tempo_map = common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("5:1", 8000)}),
        .sections =
            {
                SongSection{.position = at("1:1"), .name = "Intro"},
                SongSection{.position = at("3:1"), .name = "Verse"},
            },
        .arrangements = {},
    };
    requireValid(before);
    const SongTimeline after = landed(before, {placed(Fraction{}, 3, 4)});
    requireValid(after);
    REQUIRE(after.sections.size() == 2);
    CHECK(after.sections[0] == SongSection{.position = at("1:1"), .name = "Intro"});
    CHECK(after.sections[1] == SongSection{.position = at("3:1"), .name = "Verse"});
    CHECK(after.tempo_map.wholeNotePositionAt(after.sections[1].position) == Fraction{3, 2});
}

TEST_CASE("Sections snapping onto one downbeat fail the section rules", "[core][meter-edits]")
{
    // In 8/4 a measure is two whole notes, so Verse at whole note 1 falls inside measure 1 and
    // snaps onto Intro. The transform only snaps; the section rules the verb judges by refuse.
    const SongTimeline before{
        .tempo_map = common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("5:1", 8000)}),
        .sections =
            {
                SongSection{.position = at("1:1"), .name = "Intro"},
                SongSection{.position = at("2:1"), .name = "Verse"},
            },
        .arrangements = {},
    };
    requireValid(before);
    const SongTimeline after = landed(before, {placed(Fraction{}, 8, 4)});
    CHECK(
        after.sections == std::vector<SongSection>{
                              SongSection{.position = at("1:1"), .name = "Intro"},
                              SongSection{.position = at("1:1"), .name = "Verse"},
                          });
    const auto rules = common::core::validateSongSectionRules(after.sections, after.tempo_map);
    REQUIRE_FALSE(rules.has_value());
    CHECK(rules.error().code == common::core::SongSectionErrorCode::UnsortedOrRepeatedPositions);
}

// ---------------------------------------------------------------------------------------------
// The terminal re-seat.

TEST_CASE(
    "Terminal re-seat moves a mid-measure terminal to the next downbeat", "[core][meter-edits]")
{
    // 6:2 of 3/4 is 16 quarters; 7:1 is 18, timed from 3:1 at the last span's 500 ms quarter.
    const TempoMap map{
        {TimeSignatureChange{.measure = 1, .meter = {.numerator = 3, .denominator = 4}}},
        {anchor("1:1", 0), anchor("3:1", 3000), anchor("6:2", 8000)},
    };
    CHECK(
        reseatTerminalAnchor(map) ==
        TempoMap{
            {TimeSignatureChange{.measure = 1, .meter = {.numerator = 3, .denominator = 4}}},
            {anchor("1:1", 0), anchor("3:1", 3000), anchor("7:1", 9000)},
        });
}

TEST_CASE("Terminal re-seat rounds its time to a millisecond once", "[core][meter-edits]")
{
    // 7001 ms over 16 quarters, extended to 18: 7876.125 ms, stored as 7876.
    const TempoMap map{
        {TimeSignatureChange{.measure = 1, .meter = {.numerator = 3, .denominator = 4}}},
        {anchor("1:1", 0), anchor("6:2", 7001)},
    };
    CHECK(reseatTerminalAnchor(map).anchors().back() == anchor("7:1", 7876));
}

TEST_CASE("Terminal re-seat leaves a terminal on a downbeat", "[core][meter-edits]")
{
    const TempoMap map = sliceMap();
    CHECK(reseatTerminalAnchor(map) == map);
    // The transform does not re-seat one either: the slice fixture's terminal keeps its time.
    CHECK(respelledSlices().tempo_map.anchors().back() == anchor("6:1", 8500));
}

} // namespace rock_hero::editor::core
