/*!
\file song_timeline_fixtures.h
\brief Song timelines for the whole-song re-address suites: fixed and seeded random songs, the
rules a result must pass, and one walk pairing every position of two timelines.
*/

#pragma once

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <random>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core::testing
{

using common::core::ArrangementTimeline;
using common::core::BeatAnchor;
using common::core::Chart;
using common::core::ChartNote;
using common::core::Fraction;
using common::core::FretHandPosition;
using common::core::GridPosition;
using common::core::Keyframe;
using common::core::SongSection;
using common::core::SongTimeline;
using common::core::TempoMap;
using common::core::TimeSignature;
using common::core::TimeSignatureChange;
using common::core::ToneAutomationPoint;
using common::core::ToneParameterAutomation;
using common::core::ToneRegion;
using common::core::testing::anchor;
using common::core::testing::beatAt;
using common::core::testing::downbeat;
using common::core::testing::metersAt;

// Floating noise only: the two maps resolve one instant through different beat decompositions.
inline constexpr double g_time_tolerance_seconds = 1e-9;

[[nodiscard]] inline GridPosition at(const std::string& token)
{
    return anchor(token, 0).position;
}

[[nodiscard]] inline std::string regionId(int index)
{
    return std::format("00000000-0000-4000-8000-{:012x}", index);
}

[[nodiscard]] inline std::string toneRef(int index)
{
    return std::format("tones/00000000-0000-4000-9000-{:012x}/tone.json", index);
}

[[nodiscard]] inline ChartNote makeNote(
    GridPosition position, int string, int fret, Fraction sustain, std::vector<Keyframe> keyframes)
{
    return ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = sustain,
        .bend = 0.0,
        .keyframes = std::move(keyframes),
    };
}

// Requires a timeline every rule the package reader applies accepts.
inline void requireValid(const SongTimeline& timeline)
{
    const TempoMap& tempo_map = timeline.tempo_map;
    const auto tempo_rules = common::core::validateTempoMapRules(tempo_map);
    INFO((tempo_rules.has_value() ? std::string{} : tempo_rules.error().message));
    REQUIRE(tempo_rules.has_value());
    const auto section_rules = common::core::validateSongSectionRules(timeline.sections, tempo_map);
    INFO((section_rules.has_value() ? std::string{} : section_rules.error().message));
    REQUIRE(section_rules.has_value());
    for (const ArrangementTimeline& arrangement : timeline.arrangements)
    {
        if (arrangement.chart.has_value())
        {
            const Chart& chart = *arrangement.chart;
            const auto chart_rules = common::core::validateChartRules(chart, tempo_map);
            INFO((chart_rules.has_value() ? std::string{} : chart_rules.error().message));
            REQUIRE(chart_rules.has_value());
        }
        const auto tone_rules =
            common::core::validateToneTrackRules(arrangement.tone_track, tempo_map);
        INFO((tone_rules.has_value() ? std::string{} : tone_rules.error().message));
        REQUIRE(tone_rules.has_value());
        for (const ToneParameterAutomation& lane : arrangement.tone_automation)
        {
            REQUIRE(common::core::isValidToneParameterAutomation(lane, tempo_map));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The slice fixture: 4/4 from measure 1 and 3/4 from measure 3 at 120 BPM (a quarter is 500 ms),
// the terminal at 6:1 (4 1/4 whole notes), with a sub-beat anchor on either side of the change.
// Respelling the 3/4 as 6/8 keeps every measure's length, so no measure renumbers and only the
// addresses inside the changed reign change.

[[nodiscard]] inline TempoMap sliceMap()
{
    return TempoMap{
        metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}}),
        {
            anchor("1:1", 0),
            anchor("2:2+1/2", 2750),
            anchor("4:2+1/2", 6250),
            anchor("6:1", 8500),
        },
    };
}

[[nodiscard]] inline ArrangementTimeline sliceArrangement(
    std::string id, std::vector<ChartNote> notes)
{
    Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = std::move(notes);
    chart.fret_hand_positions = {
        FretHandPosition{.position = at("1:1"), .fret = 3, .end_fret = std::nullopt},
        FretHandPosition{.position = at("3:3+1/2"), .fret = 5, .end_fret = std::nullopt},
    };
    return ArrangementTimeline{
        .id = std::move(id),
        .chart = std::move(chart),
        .tone_track =
            common::core::ToneTrack{
                .regions =
                    {
                        ToneRegion{
                            .id = regionId(1),
                            .start = at("1:1"),
                            .tone_document_ref = toneRef(1),
                        },
                        ToneRegion{
                            .id = regionId(2),
                            .start = at("4:1+1/2"),
                            .tone_document_ref = toneRef(2),
                        },
                    },
            },
        .tone_automation = {
            ToneParameterAutomation{
                .plugin_id = "plugin",
                .param_id = "gain",
                .points = {
                    ToneAutomationPoint{.position = at("1:1"), .norm_value = 0.25F},
                    ToneAutomationPoint{.position = at("4:1+1/3"), .norm_value = 0.75F},
                },
            },
        },
    };
}

[[nodiscard]] inline SongTimeline sliceTimeline()
{
    return SongTimeline{
        .tempo_map = sliceMap(),
        .sections =
            {
                SongSection{.position = at("1:1"), .name = "Intro"},
                SongSection{.position = at("4:1"), .name = "Bridge"},
            },
        .arrangements = {
            sliceArrangement(
                "lead",
                {
                    // A ring and a keyframe from 2:4 in 4/4 across the change at 3:1.
                    makeNote(
                        at("2:4"),
                        1,
                        5,
                        Fraction{1, 2},
                        {Keyframe{.offset = Fraction{3, 8}, .bend = 1.0}}),
                    makeNote(at("4:2"), 2, 7, Fraction{1, 8}, {}),
                }),
            sliceArrangement("rhythm", {makeNote(at("5:3"), 3, 2, Fraction{1, 8}, {})}),
        },
    };
}

// The fixture's chart for one arrangement; every fixture arrangement has one.
[[nodiscard]] inline const Chart& chartOf(const SongTimeline& timeline, std::size_t arrangement)
{
    REQUIRE(arrangement < timeline.arrangements.size());
    const std::optional<Chart>& chart = timeline.arrangements[arrangement].chart;
    REQUIRE(chart.has_value());
    if (!chart.has_value())
    {
        throw std::logic_error{"fixture arrangement without a chart"};
    }
    return *chart;
}

// ---------------------------------------------------------------------------------------------
// Random songs. Rolled by modulo off the standard engine rather than through a standard
// distribution, whose output differs between standard libraries, so a seed names the same song on
// every platform.

class Dice
{
public:
    explicit Dice(std::uint32_t seed)
        : m_engine{seed}
    {}

    // A value in [low, high].
    [[nodiscard]] int roll(int low, int high)
    {
        return low + static_cast<int>(m_engine() % static_cast<std::uint32_t>(high - low + 1));
    }

    [[nodiscard]] bool chance(int percent)
    {
        return roll(1, 100) <= percent;
    }

private:
    std::mt19937 m_engine;
};

// Each length in two spellings, so a respelling exists, and small measures, so a random
// restatement often divides a reign.
inline constexpr std::array<TimeSignature, 12> g_meters{{
    {.numerator = 4, .denominator = 4},
    {.numerator = 3, .denominator = 4},
    {.numerator = 7, .denominator = 8},
    {.numerator = 6, .denominator = 8},
    {.numerator = 5, .denominator = 4},
    {.numerator = 2, .denominator = 2},
    {.numerator = 12, .denominator = 16},
    {.numerator = 5, .denominator = 8},
    {.numerator = 3, .denominator = 8},
    {.numerator = 9, .denominator = 16},
    {.numerator = 2, .denominator = 4},
    {.numerator = 1, .denominator = 4},
}};

[[nodiscard]] inline TimeSignature randomMeter(Dice& dice)
{
    return g_meters.at(
        static_cast<std::size_t>(dice.roll(0, static_cast<int>(g_meters.size()) - 1)));
}

// The same measure length in a different denominator, which always divides the reign it replaces.
[[nodiscard]] inline TimeSignature respelled(TimeSignature meter)
{
    if (meter.denominator <= 32)
    {
        return TimeSignature{
            .numerator = meter.numerator * 2, .denominator = meter.denominator * 2
        };
    }
    return TimeSignature{.numerator = meter.numerator / 2, .denominator = meter.denominator / 2};
}

// Lattice grids content is rolled on: straight, triplet, quintuplet and the bare tick.
inline constexpr std::array<int, 6> g_grids{16, 12, 24, 32, 20, 3840};

// A strictly positive length on a random grid, at most half a whole note.
[[nodiscard]] inline Fraction randomLength(Dice& dice)
{
    const int grid = g_grids.at(static_cast<std::size_t>(dice.roll(0, 5)));
    return Fraction{dice.roll(1, grid / 2), grid};
}

// A whole-note place on the 1/96 lattice strictly inside (0, limit), nudged a tick late at times.
[[nodiscard]] inline Fraction randomPlace(Dice& dice, Fraction limit)
{
    const Fraction slots = limit * Fraction{96};
    const Fraction place{dice.roll(1, (slots.numerator / slots.denominator) - 1), 96};
    return dice.chance(30) ? place + Fraction{1, 3840} : place;
}

// Distinct places in (0, limit), ascending.
[[nodiscard]] inline std::vector<Fraction> randomPlaces(Dice& dice, Fraction limit, int count)
{
    std::set<std::pair<int, int>> seen;
    std::vector<Fraction> places;
    for (int index = 0; index < count; ++index)
    {
        const Fraction place = randomPlace(dice, limit);
        if (seen.insert({place.numerator, place.denominator}).second)
        {
            places.push_back(place);
        }
    }
    std::ranges::sort(places);
    return places;
}

[[nodiscard]] inline GridPosition positionAt(const TempoMap& tempo_map, Fraction whole_note)
{
    return common::core::advanceGridPositionByWholeNotes(tempo_map, GridPosition{}, whole_note);
}

[[nodiscard]] inline TempoMap randomTempoMap(Dice& dice)
{
    // Each change a whole number of the previous meter's measures after it, so every reign
    // divides and the measures count up as a charter would number them.
    std::vector<TimeSignatureChange> signatures;
    int measure = 1;
    Fraction change_place{};
    const int changes = dice.roll(0, 4);
    for (int index = 0; index <= changes; ++index)
    {
        const TimeSignature meter = randomMeter(dice);
        signatures.push_back(TimeSignatureChange{.place = change_place, .meter = meter});
        const int measures = dice.roll(1, 5);
        measure += measures;
        change_place = change_place + meter.measureWholeNotes() * Fraction{measures};
    }
    const int terminal_measure = measure + dice.roll(0, 3);

    // Addresses only, so its anchors' times are placeholders.
    const TempoMap grid{
        signatures,
        {anchor("1:1", 0), BeatAnchor{.position = downbeat(terminal_measure), .milliseconds = 1}}
    };
    const Fraction terminal_whole_note = grid.wholeNotePositionAt(downbeat(terminal_measure));
    std::vector<Fraction> places = randomPlaces(dice, terminal_whole_note, dice.roll(0, 4));
    places.push_back(terminal_whole_note);

    std::vector<BeatAnchor> anchors{anchor("1:1", 0)};
    Fraction previous_place{};
    for (const Fraction& place : places)
    {
        const double quarters = 4.0 * (place - previous_place).toDouble();
        const auto span =
            static_cast<std::int64_t>(quarters * 60000.0 / static_cast<double>(dice.roll(60, 200)));
        anchors.push_back(
            BeatAnchor{
                .position = positionAt(grid, place),
                .milliseconds = anchors.back().milliseconds + std::max<std::int64_t>(1, span),
            });
        previous_place = place;
    }
    return TempoMap{std::move(signatures), std::move(anchors)};
}

[[nodiscard]] inline std::vector<ChartNote> randomNotes(Dice& dice, const TempoMap& tempo_map)
{
    const Fraction content_end =
        tempo_map.wholeNotePositionAt(common::core::terminalGridPosition(tempo_map));
    std::vector<ChartNote> notes;
    for (int string = 1; string <= 6; ++string)
    {
        Fraction cursor = dice.chance(50) ? Fraction{} : randomLength(dice);
        while (true)
        {
            const int grid = g_grids.at(static_cast<std::size_t>(dice.roll(0, 5)));
            const int units = dice.roll(1, std::max(1, grid / 2));
            const Fraction ring{units, grid};
            if (cursor + ring >= content_end)
            {
                break;
            }
            const GridPosition onset = positionAt(tempo_map, cursor);
            const int fret = dice.roll(1, 15);
            std::vector<Keyframe> keyframes;
            if (dice.chance(25))
            {
                // A slide reaching another fret where the ring ends.
                keyframes.push_back(Keyframe{.offset = ring, .fret = fret + 2});
            }
            else if (dice.chance(30) && units >= 2)
            {
                // A bend rising inside the ring and released at its end.
                const Fraction rise{dice.roll(1, units - 1), grid};
                keyframes.push_back(Keyframe{.offset = rise, .bend = 1.0});
                keyframes.push_back(Keyframe{.offset = ring, .bend = 0.0});
            }
            notes.push_back(makeNote(onset, string, fret, ring, std::move(keyframes)));
            cursor = cursor + ring;
            if (dice.chance(60))
            {
                cursor = cursor + randomLength(dice);
            }
        }
    }
    std::ranges::sort(notes, [](const ChartNote& lhs, const ChartNote& rhs) {
        return lhs.position != rhs.position ? lhs.position < rhs.position : lhs.string < rhs.string;
    });
    return notes;
}

[[nodiscard]] inline ArrangementTimeline randomArrangement(
    Dice& dice, const TempoMap& tempo_map, std::string id, int tone_base)
{
    const Fraction content_end =
        tempo_map.wholeNotePositionAt(common::core::terminalGridPosition(tempo_map));

    Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = randomNotes(dice, tempo_map);
    for (const Fraction& place : randomPlaces(dice, content_end, dice.roll(0, 5)))
    {
        chart.fret_hand_positions.push_back(
            FretHandPosition{
                .position = positionAt(tempo_map, place),
                .fret = dice.roll(1, 12),
                .end_fret = std::nullopt,
            });
    }
    // The load path's normalizer settles whatever the dice made that a rule repairs.
    static_cast<void>(common::core::normalizeChart(chart, tempo_map));

    common::core::ToneTrack tone_track{
        .regions = {ToneRegion{
            .id = regionId(tone_base),
            .start = downbeat(1),
            .tone_document_ref = toneRef(tone_base),
        }},
    };
    int region = tone_base;
    for (const Fraction& place : randomPlaces(dice, content_end, dice.roll(0, 3)))
    {
        ++region;
        tone_track.regions.push_back(
            ToneRegion{
                .id = regionId(region),
                .start = positionAt(tempo_map, place),
                .tone_document_ref = toneRef(region),
            });
    }

    std::vector<ToneParameterAutomation> tone_automation;
    const int lanes = dice.roll(0, 2);
    for (int lane = 0; lane < lanes; ++lane)
    {
        ToneParameterAutomation automation{
            .plugin_id = std::format("plugin-{}", lane),
            .param_id = "gain",
            .points = {},
        };
        for (const Fraction& place : randomPlaces(dice, content_end, dice.roll(1, 4)))
        {
            automation.points.push_back(
                ToneAutomationPoint{.position = positionAt(tempo_map, place), .norm_value = 0.5F});
        }
        if (!automation.points.empty())
        {
            tone_automation.push_back(std::move(automation));
        }
    }

    return ArrangementTimeline{
        .id = std::move(id),
        .chart = std::move(chart),
        .tone_track = std::move(tone_track),
        .tone_automation = std::move(tone_automation),
    };
}

[[nodiscard]] inline SongTimeline randomTimeline(Dice& dice)
{
    TempoMap tempo_map = randomTempoMap(dice);
    const int terminal_measure = common::core::terminalGridPosition(tempo_map).measure;
    std::vector<SongSection> sections;
    std::set<int> measures;
    const int count = dice.roll(0, 5);
    for (int index = 0; index < count; ++index)
    {
        measures.insert(dice.roll(1, terminal_measure - 1));
    }
    for (const int measure : measures)
    {
        // A section is an instant anywhere on the grid, so half of them stand on beat 2 of their
        // measure where it has one, to put the round trip through a mid-measure section too.
        const bool on_beat_two =
            dice.roll(0, 1) == 1 && tempo_map.timeSignatureAt(measure).meter.numerator >= 2;
        sections.push_back(
            SongSection{
                .position = on_beat_two ? beatAt(measure, 2) : downbeat(measure),
                .name = std::format("Section {}", measure),
            });
    }
    std::vector<ArrangementTimeline> arrangements;
    arrangements.push_back(randomArrangement(dice, tempo_map, "lead", 1));
    arrangements.push_back(randomArrangement(dice, tempo_map, "bass", 100));
    return SongTimeline{
        .tempo_map = std::move(tempo_map),
        .sections = std::move(sections),
        .arrangements = std::move(arrangements),
    };
}

// A one-arrangement timeline holding one note, under a map with no meter change.
[[nodiscard]] inline SongTimeline oneNoteTimeline(TempoMap tempo_map, ChartNote note)
{
    Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {std::move(note)};
    return SongTimeline{
        .tempo_map = std::move(tempo_map),
        .sections = {},
        .arrangements = {ArrangementTimeline{
            .id = "lead",
            .chart = std::move(chart),
            .tone_track =
                common::core::ToneTrack{
                    .regions = {ToneRegion{
                        .id = regionId(1),
                        .start = at("1:1"),
                        .tone_document_ref = toneRef(1),
                    }},
                },
            .tone_automation = {},
        }},
    };
}

// ---------------------------------------------------------------------------------------------
// One walk over every position two timelines hold, paired.

// What a paired position addresses.
enum class SliceKind : std::uint8_t
{
    Section,
    NoteOnset,
    RingEnd,
    Keyframe,
    HandPosition,
    ToneRegionStart,
    AutomationPoint,
};

// One position of a timeline and the same object's position in another, with the note onsets a
// ring end or a keyframe counts from (the position itself for every other kind).
struct SlicePair
{
    SliceKind kind{};
    GridPosition old_position;
    GridPosition new_position;
    GridPosition old_onset;
    GridPosition new_onset;
};

// Calls check once per position: the sections, then per arrangement each note's onset, ring end
// and keyframes, its hand positions, its tone region starts and its automation points. Every list
// must hold as many entries in both timelines.
template <typename Check>
void forEachSlicePair(const SongTimeline& before, const SongTimeline& after, const Check& check)
{
    const TempoMap& old_map = before.tempo_map;
    const TempoMap& new_map = after.tempo_map;
    const auto plain = [&check](
                           const SliceKind kind,
                           const GridPosition& old_position,
                           const GridPosition& new_position) {
        check(
            SlicePair{
                .kind = kind,
                .old_position = old_position,
                .new_position = new_position,
                .old_onset = old_position,
                .new_onset = new_position,
            });
    };

    REQUIRE(after.sections.size() == before.sections.size());
    for (std::size_t index = 0; index < before.sections.size(); ++index)
    {
        plain(SliceKind::Section, before.sections[index].position, after.sections[index].position);
    }
    REQUIRE(after.arrangements.size() == before.arrangements.size());
    for (std::size_t arrangement = 0; arrangement < before.arrangements.size(); ++arrangement)
    {
        const ArrangementTimeline& old_share = before.arrangements[arrangement];
        const ArrangementTimeline& new_share = after.arrangements[arrangement];
        REQUIRE(new_share.chart.has_value() == old_share.chart.has_value());
        if (old_share.chart.has_value() && new_share.chart.has_value())
        {
            const Chart& old_chart = *old_share.chart;
            const Chart& new_chart = *new_share.chart;
            REQUIRE(new_chart.notes.size() == old_chart.notes.size());
            for (std::size_t index = 0; index < old_chart.notes.size(); ++index)
            {
                const ChartNote& old_note = old_chart.notes[index];
                const ChartNote& new_note = new_chart.notes[index];
                plain(SliceKind::NoteOnset, old_note.position, new_note.position);
                check(
                    SlicePair{
                        .kind = SliceKind::RingEnd,
                        .old_position = common::core::advanceGridPositionByWholeNotes(
                            old_map, old_note.position, old_note.sustain),
                        .new_position = common::core::advanceGridPositionByWholeNotes(
                            new_map, new_note.position, new_note.sustain),
                        .old_onset = old_note.position,
                        .new_onset = new_note.position,
                    });
                REQUIRE(new_note.keyframes.size() == old_note.keyframes.size());
                for (std::size_t key = 0; key < old_note.keyframes.size(); ++key)
                {
                    check(
                        SlicePair{
                            .kind = SliceKind::Keyframe,
                            .old_position = common::core::advanceGridPositionByWholeNotes(
                                old_map, old_note.position, old_note.keyframes[key].offset),
                            .new_position = common::core::advanceGridPositionByWholeNotes(
                                new_map, new_note.position, new_note.keyframes[key].offset),
                            .old_onset = old_note.position,
                            .new_onset = new_note.position,
                        });
                }
            }
            REQUIRE(new_chart.fret_hand_positions.size() == old_chart.fret_hand_positions.size());
            for (std::size_t index = 0; index < old_chart.fret_hand_positions.size(); ++index)
            {
                plain(
                    SliceKind::HandPosition,
                    old_chart.fret_hand_positions[index].position,
                    new_chart.fret_hand_positions[index].position);
            }
        }
        REQUIRE(new_share.tone_track.regions.size() == old_share.tone_track.regions.size());
        for (std::size_t index = 0; index < old_share.tone_track.regions.size(); ++index)
        {
            plain(
                SliceKind::ToneRegionStart,
                old_share.tone_track.regions[index].start,
                new_share.tone_track.regions[index].start);
        }
        REQUIRE(new_share.tone_automation.size() == old_share.tone_automation.size());
        for (std::size_t lane = 0; lane < old_share.tone_automation.size(); ++lane)
        {
            const std::vector<ToneAutomationPoint>& old_points =
                old_share.tone_automation[lane].points;
            const std::vector<ToneAutomationPoint>& new_points =
                new_share.tone_automation[lane].points;
            REQUIRE(new_points.size() == old_points.size());
            for (std::size_t index = 0; index < old_points.size(); ++index)
            {
                plain(
                    SliceKind::AutomationPoint,
                    old_points[index].position,
                    new_points[index].position);
            }
        }
    }
}

// Half the longer of the two ticks beside a position: the most a rounding onto the lattice moves it
// in time.
[[nodiscard]] inline double halfTickSeconds(const TempoMap& tempo_map, const GridPosition& position)
{
    const Fraction tick = common::core::g_tick_quantum_note_value;
    const double here = tempo_map.secondsAt(position);
    const double later =
        tempo_map.secondsAt(
            common::core::advanceGridPositionByWholeNotes(tempo_map, position, tick)) -
        here;
    const double earlier =
        here - tempo_map.secondsAt(
                   common::core::advanceGridPositionByWholeNotes(
                       tempo_map, position, Fraction{-tick.numerator, tick.denominator}));
    return std::max(later, earlier) / 2.0;
}

// Checks every position of a re-addressed timeline, sections included, against the one it came
// from: its time held within half a tick of the new grid, the most the rounding onto the lattice
// moves it, plus `slack_seconds` where the edit itself re-timed a span.
inline void checkTimesHeld(
    const SongTimeline& before, const SongTimeline& after, const double slack_seconds)
{
    const TempoMap& old_map = before.tempo_map;
    const TempoMap& new_map = after.tempo_map;
    forEachSlicePair(before, after, [&old_map, &new_map, slack_seconds](const SlicePair& pair) {
        CHECK_THAT(
            new_map.secondsAt(pair.new_position),
            Catch::Matchers::WithinAbs(
                old_map.secondsAt(pair.old_position),
                halfTickSeconds(new_map, pair.new_position) + slack_seconds +
                    g_time_tolerance_seconds));
    });
}

// The timeline's sections and arrangements with every address the re-address writes cleared (each
// position, ring and keyframe offset), so two timelines compare equal exactly when the re-address
// left every other field alone.
[[nodiscard]] inline SongTimeline addressesErased(SongTimeline timeline)
{
    timeline.tempo_map = TempoMap{};
    for (SongSection& section : timeline.sections)
    {
        section.position = {};
    }
    for (ArrangementTimeline& share : timeline.arrangements)
    {
        if (share.chart.has_value())
        {
            Chart& chart = *share.chart;
            for (ChartNote& note : chart.notes)
            {
                note.position = {};
                note.sustain = {};
                for (Keyframe& keyframe : note.keyframes)
                {
                    keyframe.offset = {};
                }
            }
            for (FretHandPosition& placement : chart.fret_hand_positions)
            {
                placement.position = {};
            }
        }
        for (ToneRegion& region : share.tone_track.regions)
        {
            region.start = {};
        }
        for (ToneParameterAutomation& lane : share.tone_automation)
        {
            for (ToneAutomationPoint& point : lane.points)
            {
                point.position = {};
            }
        }
    }
    return timeline;
}

} // namespace rock_hero::editor::core::testing
