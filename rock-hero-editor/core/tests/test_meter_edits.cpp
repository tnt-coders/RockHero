#include "timeline/meter_edits.h"
#include "timeline/tempo_map_edits.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
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
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
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
using common::core::testing::downbeat;

using Catch::Matchers::WithinAbs;

// Floating noise only: the two maps resolve one instant through different beat decompositions.
constexpr double g_time_tolerance_seconds = 1e-9;

// A re-seated terminal re-times the last span, its whole-millisecond rounding shifting the span's
// tempo by at most half a millisecond over the span's length.
constexpr double g_reseated_time_tolerance_seconds = 0.5e-3 + g_time_tolerance_seconds;

[[nodiscard]] GridPosition at(const std::string& token)
{
    return anchor(token, 0).position;
}

[[nodiscard]] PlacedMeter placed(Fraction whole_note, int numerator, int denominator)
{
    return PlacedMeter{
        .whole_note = whole_note,
        .meter = {.numerator = numerator, .denominator = denominator},
    };
}

[[nodiscard]] std::string regionId(int index)
{
    return std::format("00000000-0000-4000-8000-{:012x}", index);
}

[[nodiscard]] std::string toneRef(int index)
{
    return std::format("tones/00000000-0000-4000-9000-{:012x}/tone.json", index);
}

[[nodiscard]] ChartNote makeNote(
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
void requireValid(const SongTimeline& timeline)
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

// Re-addresses and requires the edit to land.
[[nodiscard]] SongTimeline landed(
    const SongTimeline& timeline, const std::vector<PlacedMeter>& meters)
{
    const std::expected<SongTimeline, MeterEditError> result =
        readdressUnderMeters(timeline, meters);
    INFO((result.has_value() ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    return *result;
}

// ---------------------------------------------------------------------------------------------
// The slice fixture: 4/4 from measure 1 and 3/4 from measure 3 at 120 BPM (a quarter is 500 ms),
// the terminal at 6:1 (4 1/4 whole notes), with a sub-beat anchor on either side of the change.
// Respelling the 3/4 as 6/8 keeps every measure's length, so no measure renumbers and only the
// addresses inside the changed reign, and the beat counts crossing into it, change.

[[nodiscard]] TempoMap sliceMap()
{
    return TempoMap{
        {
            TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
            TimeSignatureChange{.measure = 3, .meter = {.numerator = 3, .denominator = 4}},
        },
        {
            anchor("1:1", 0),
            anchor("2:2+1/2", 2750),
            anchor("4:2+1/2", 6250),
            anchor("6:1", 8500),
        },
    };
}

[[nodiscard]] ArrangementTimeline sliceArrangement(std::string id, std::vector<ChartNote> notes)
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

[[nodiscard]] SongTimeline sliceTimeline()
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
                        Fraction{2},
                        {Keyframe{.offset = Fraction{3, 2}, .bend = 1.0}}),
                    makeNote(at("4:2"), 2, 7, Fraction{1, 2}, {}),
                }),
            sliceArrangement("rhythm", {makeNote(at("5:3"), 3, 2, Fraction{1, 2}, {})}),
        },
    };
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

// The fixture's chart for one arrangement; every fixture arrangement has one.
[[nodiscard]] const Chart& chartOf(const SongTimeline& timeline, std::size_t arrangement)
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
constexpr std::array<TimeSignature, 12> g_meters{{
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

[[nodiscard]] TimeSignature randomMeter(Dice& dice)
{
    return g_meters.at(
        static_cast<std::size_t>(dice.roll(0, static_cast<int>(g_meters.size()) - 1)));
}

// The same measure length in a different denominator, which always divides the reign it replaces.
[[nodiscard]] TimeSignature respelled(TimeSignature meter)
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
constexpr std::array<int, 6> g_grids{16, 12, 24, 32, 20, 3840};

// A strictly positive length on a random grid, at most half a whole note.
[[nodiscard]] Fraction randomLength(Dice& dice)
{
    const int grid = g_grids.at(static_cast<std::size_t>(dice.roll(0, 5)));
    return Fraction{dice.roll(1, grid / 2), grid};
}

// A whole-note place on the 1/96 lattice strictly inside (0, limit), nudged a tick late at times.
[[nodiscard]] Fraction randomPlace(Dice& dice, Fraction limit)
{
    const Fraction slots = limit * Fraction{96};
    const Fraction place{dice.roll(1, (slots.numerator / slots.denominator) - 1), 96};
    return dice.chance(30) ? place + Fraction{1, 3840} : place;
}

// Distinct places in (0, limit), ascending.
[[nodiscard]] std::vector<Fraction> randomPlaces(Dice& dice, Fraction limit, int count)
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

[[nodiscard]] GridPosition positionAt(const TempoMap& tempo_map, Fraction whole_note)
{
    return common::core::advanceGridPositionByWholeNotes(tempo_map, GridPosition{}, whole_note);
}

[[nodiscard]] TempoMap randomTempoMap(Dice& dice)
{
    std::vector<TimeSignatureChange> signatures;
    int measure = 1;
    const int changes = dice.roll(0, 4);
    for (int index = 0; index <= changes; ++index)
    {
        signatures.push_back(TimeSignatureChange{.measure = measure, .meter = randomMeter(dice)});
        measure += dice.roll(1, 5);
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

[[nodiscard]] std::vector<ChartNote> randomNotes(Dice& dice, const TempoMap& tempo_map)
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
                keyframes.push_back(
                    Keyframe{
                        .offset = common::core::beatDistance(
                            tempo_map, onset, positionAt(tempo_map, cursor + ring)),
                        .fret = fret + 2,
                    });
            }
            else if (dice.chance(30) && units >= 2)
            {
                // A bend rising inside the ring and released at its end.
                const Fraction rise{dice.roll(1, units - 1), grid};
                keyframes.push_back(
                    Keyframe{
                        .offset = common::core::beatDistance(
                            tempo_map, onset, positionAt(tempo_map, cursor + rise)),
                        .bend = 1.0,
                    });
                keyframes.push_back(
                    Keyframe{
                        .offset = common::core::beatDistance(
                            tempo_map, onset, positionAt(tempo_map, cursor + ring)),
                        .bend = 0.0,
                    });
            }
            notes.push_back(makeNote(
                onset,
                string,
                fret,
                common::core::beatDistance(tempo_map, onset, positionAt(tempo_map, cursor + ring)),
                std::move(keyframes)));
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

[[nodiscard]] ArrangementTimeline randomArrangement(
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

[[nodiscard]] SongTimeline randomTimeline(Dice& dice)
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
        sections.push_back(
            SongSection{.position = downbeat(measure), .name = std::format("Section {}", measure)});
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
    // durations the re-expression exists for.
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
    const auto keepsPlace = [&old_map, &new_map, tolerance](
                                const GridPosition& old_position,
                                const GridPosition& new_position) {
        CHECK(
            new_map.wholeNotePositionAt(new_position) == old_map.wholeNotePositionAt(old_position));
        CHECK_THAT(
            new_map.secondsAt(new_position), WithinAbs(old_map.secondsAt(old_position), tolerance));
    };

    REQUIRE(after.sections.size() == before.sections.size());
    for (std::size_t index = 0; index < before.sections.size(); ++index)
    {
        const SongSection& old_section = before.sections[index];
        const SongSection& new_section = after.sections[index];
        CHECK(new_section.name == old_section.name);
        const Fraction old_place = old_map.wholeNotePositionAt(old_section.position);
        const Fraction new_place = new_map.wholeNotePositionAt(new_section.position);
        footprint.section_snapped.push_back(new_place != old_place);
        if (new_place == old_place)
        {
            keepsPlace(old_section.position, new_section.position);
            continue;
        }
        // Snapped back to the downbeat of the measure it now falls in.
        CHECK(new_place < old_place);
        CHECK(common::core::measureDownbeat(new_section.position) == new_section.position);
        CHECK(old_place < new_map.wholeNotePositionAt(downbeat(new_section.position.measure + 1)));
    }

    REQUIRE(after.arrangements.size() == before.arrangements.size());
    for (std::size_t arrangement = 0; arrangement < before.arrangements.size(); ++arrangement)
    {
        const ArrangementTimeline& old_share = before.arrangements[arrangement];
        const ArrangementTimeline& new_share = after.arrangements[arrangement];
        CHECK(new_share.id == old_share.id);

        // Every field but the re-addressed and re-expressed ones is compared whole, by writing the
        // new values into a copy of the old share.
        ArrangementTimeline expected = old_share;
        REQUIRE(new_share.chart.has_value() == old_share.chart.has_value());
        if (old_share.chart.has_value() && new_share.chart.has_value() &&
            expected.chart.has_value())
        {
            const Chart& old_chart = *old_share.chart;
            const Chart& new_chart = *new_share.chart;
            Chart& expected_chart = *expected.chart;
            REQUIRE(new_chart.notes.size() == old_chart.notes.size());
            for (std::size_t index = 0; index < old_chart.notes.size(); ++index)
            {
                const ChartNote& old_note = old_chart.notes[index];
                const ChartNote& new_note = new_chart.notes[index];
                keepsPlace(old_note.position, new_note.position);
                const GridPosition old_end = common::core::sustainEndPosition(old_map, old_note);
                const GridPosition new_end = common::core::sustainEndPosition(new_map, new_note);
                keepsPlace(old_end, new_end);
                if (crossesDenominator(
                        old_map, old_note.position, old_end, new_map, new_note.position, new_end))
                {
                    ++footprint.crossing_rings;
                }
                REQUIRE(new_note.keyframes.size() == old_note.keyframes.size());
                ChartNote& expected_note = expected_chart.notes[index];
                expected_note.position = new_note.position;
                expected_note.sustain = new_note.sustain;
                for (std::size_t key = 0; key < old_note.keyframes.size(); ++key)
                {
                    const GridPosition old_key = common::core::advanceGridPosition(
                        old_map, old_note.position, old_note.keyframes[key].offset);
                    const GridPosition new_key = common::core::advanceGridPosition(
                        new_map, new_note.position, new_note.keyframes[key].offset);
                    keepsPlace(old_key, new_key);
                    if (crossesDenominator(
                            old_map,
                            old_note.position,
                            old_key,
                            new_map,
                            new_note.position,
                            new_key))
                    {
                        ++footprint.crossing_keyframes;
                    }
                    expected_note.keyframes[key].offset = new_note.keyframes[key].offset;
                }
            }
            REQUIRE(new_chart.fret_hand_positions.size() == old_chart.fret_hand_positions.size());
            for (std::size_t index = 0; index < old_chart.fret_hand_positions.size(); ++index)
            {
                keepsPlace(
                    old_chart.fret_hand_positions[index].position,
                    new_chart.fret_hand_positions[index].position);
                expected_chart.fret_hand_positions[index].position =
                    new_chart.fret_hand_positions[index].position;
            }
        }
        REQUIRE(new_share.tone_track.regions.size() == old_share.tone_track.regions.size());
        for (std::size_t index = 0; index < old_share.tone_track.regions.size(); ++index)
        {
            keepsPlace(
                old_share.tone_track.regions[index].start,
                new_share.tone_track.regions[index].start);
            expected.tone_track.regions[index].start = new_share.tone_track.regions[index].start;
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
                keepsPlace(old_points[index].position, new_points[index].position);
                expected.tone_automation[lane].points[index].position = new_points[index].position;
            }
        }
        CHECK(new_share == expected);
    }
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

        const std::expected<SongTimeline, MeterEditError> after =
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
        const std::expected<SongTimeline, MeterEditError> back =
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

TEST_CASE("Meter edit re-expresses a ring crossing a denominator change", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE_FALSE(chart.notes.empty());
    // From 2:4, two beats ran one quarter of 4/4 and one of 3/4 to 3:2; in 6/8 the same end is
    // 3:3, one quarter and two eighths away.
    CHECK(chart.notes[0].sustain == Fraction{3});
    CHECK(common::core::sustainEndPosition(after.tempo_map, chart.notes[0]) == at("3:3"));
    // A ring inside the respelled reign doubles with the beat it counts.
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[1].sustain == Fraction{1});
}

TEST_CASE("Meter edit re-expresses a keyframe crossing a denominator change", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE_FALSE(chart.notes.empty());
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    // One and a half beats from 2:4 reached 3:1+1/2 in 3/4, which is 3:2 in 6/8: two beats.
    CHECK(chart.notes[0].keyframes[0].offset == Fraction{2});
    CHECK(chart.notes[0].keyframes[0].bend == std::optional{1.0});
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
    const std::expected<SongTimeline, MeterEditError> after = readdressUnderMeters(
        before, std::vector{placed(Fraction{}, 3, 4), placed(Fraction{4}, 6, 8)});
    REQUIRE_FALSE(after.has_value());
    CHECK(after.error().code == MeterEditErrorCode::MeterOffDownbeat);
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
