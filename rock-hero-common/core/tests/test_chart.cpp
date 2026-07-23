#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

namespace
{

// A 4/4 default map long enough that measures one through four are valid grid.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// One chart exercising every construct the format defines.
[[nodiscard]] Chart makeFullChart()
{
    Chart chart;
    chart.tuning = ChartTuning{
        .strings = {"C2", "G2", "C3", "F3", "A3", "D4"},
        .capo = 2,
        .cent_offset = -6.5,
    };
    chart.templates = {
        ChordTemplate{
            .name = "F5",
            .frets = {1, 3, 3, std::nullopt, std::nullopt, std::nullopt},
            .fingers = {1, 3, 4, std::nullopt, std::nullopt, std::nullopt},
        },
        ChordTemplate{
            .name = "",
            .frets = {std::nullopt, 10, 8, 7, std::nullopt, std::nullopt},
            .fingers = {std::nullopt, 4, 2, 1, std::nullopt, std::nullopt},
        },
    };
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 0,
            .mute = NoteMute::Palm,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}},
            .string = 3,
            .fret = 7,
            .attack = NoteAttack::Hammer,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 2},
            .string = 4,
            .fret = 12,
            .sustain = Fraction{1, 2},
            .attack = NoteAttack::Tap,
            .bend = {},
            .slides = {SlideWaypoint{.offset = Fraction{1, 4}, .fret = 13}},
            .slide_out = SlideOut{.offset = Fraction{1, 2}, .fret = 15},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{4},
            .vibrato = true,
            .bend =
                {
                    BendPoint{.offset = Fraction{0}, .semitones = 0.0},
                    BendPoint{.offset = Fraction{1, 2}, .semitones = 0.5},
                    BendPoint{.offset = Fraction{2}, .semitones = 2.0},
                    BendPoint{.offset = Fraction{4}, .semitones = 0.0},
                },
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{1},
            .harmonic = NoteHarmonic::Pinch,
            .touch = 3.2,
            .tremolo = true,
            .accent = true,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2},
            .string = 6,
            .fret = 3,
            .sustain = Fraction{1, 12},
            .attack = NoteAttack::Slap,
            .bend = {},
            // A shift-slide glide: a pitched waypoint at the sustain end, the minimum note
            // distance before the re-picked landing (the 3:2+1/3 note below).
            .slides = {SlideWaypoint{.offset = Fraction{1, 12}, .fret = 5}},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2, .offset = Fraction{1, 3}},
            .string = 6,
            .fret = 5,
            .attack = NoteAttack::Pop,
            .mute = NoteMute::Full,
            .bend = {},
            .slides = {},
        },
    };
    chart.shapes = {
        ChartShape{
            .position = GridPosition{.measure = 1, .beat = 1},
            .sustain = Fraction{2},
            .chord = 0,
        },
        ChartShape{
            .position = GridPosition{.measure = 3, .beat = 1},
            .sustain = Fraction{11, 8},
            .chord = 1,
        },
    };
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 1, .beat = 1}, .fret = 5},
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 7, .width = 5},
    };
    return chart;
}

} // namespace

TEST_CASE("Chart grid position tokens round-trip", "[core][chart]")
{
    const auto whole = parseGridPositionToken("12:3");
    REQUIRE(whole.has_value());
    if (whole.has_value())
    {
        CHECK(*whole == GridPosition{.measure = 12, .beat = 3});
        CHECK(formatGridPositionToken(*whole) == "12:3");
    }

    const auto fractional = parseGridPositionToken("12:3+1/2");
    REQUIRE(fractional.has_value());
    if (fractional.has_value())
    {
        CHECK(*fractional == GridPosition{.measure = 12, .beat = 3, .offset = Fraction{1, 2}});
        CHECK(formatGridPositionToken(*fractional) == "12:3+1/2");
    }

    // Non-canonical spellings parse to the reduced value and reformat canonically.
    const auto reducible = parseGridPositionToken("4:1+2/4");
    REQUIRE(reducible.has_value());
    if (reducible.has_value())
    {
        CHECK(reducible->offset == Fraction{1, 2});
    }

    CHECK_FALSE(parseGridPositionToken("").has_value());
    CHECK_FALSE(parseGridPositionToken("12").has_value());
    CHECK_FALSE(parseGridPositionToken("0:1").has_value());
    CHECK_FALSE(parseGridPositionToken("1:0").has_value());
    CHECK_FALSE(parseGridPositionToken("1:1+").has_value());
    CHECK_FALSE(parseGridPositionToken("1:1+1/0").has_value());
    CHECK_FALSE(parseGridPositionToken("1:1+2/2").has_value());
    CHECK_FALSE(parseGridPositionToken("1:1+3/2").has_value());
    CHECK_FALSE(parseGridPositionToken("1:1+0/2").has_value());
}

TEST_CASE("Chart beat fraction tokens round-trip", "[core][chart]")
{
    CHECK(parseBeatFractionToken("2") == Fraction{2});
    CHECK(parseBeatFractionToken("11/8") == Fraction{11, 8});
    CHECK(formatBeatFractionToken(Fraction{2}) == "2");
    CHECK(formatBeatFractionToken(Fraction{11, 8}) == "11/8");
    CHECK_FALSE(parseBeatFractionToken("").has_value());
    CHECK_FALSE(parseBeatFractionToken("1/").has_value());
    CHECK_FALSE(parseBeatFractionToken("/2").has_value());
    CHECK_FALSE(parseBeatFractionToken("-1/2").has_value());
}

TEST_CASE("Chart document round-trips every construct", "[core][chart]")
{
    const Chart chart = makeFullChart();

    const std::string text = chartDocumentText(chart);
    const auto parsed = parseChartDocument(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == chart);

    // The full fixture also satisfies the structural rules.
    CHECK(validateChartRules(chart, makeTempoMap()).has_value());
}

TEST_CASE("Chart document rejects unsupported versions", "[core][chart]")
{
    // Missing and non-1 versions are both rejected by the single chart version gate.
    CHECK_FALSE(parseChartDocument(R"({ "tuning": { "strings": ["E2"] } })").has_value());
    CHECK_FALSE(parseChartDocument(R"({ "formatVersion": 2, "tuning": { "strings": ["E2"] } })")
                    .has_value());

    const auto rejected =
        parseChartDocument(R"({ "formatVersion": 2, "tuning": { "strings": ["E2"] } })");
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().message.find("formatVersion") != std::string::npos);
}

TEST_CASE("Chart document rejects malformed elements", "[core][chart]")
{
    CHECK_FALSE(parseChartDocument("not json").has_value());
    CHECK_FALSE(
        parseChartDocument(R"({ "formatVersion": 1, "tuning": { "strings": 3 } })").has_value());
    CHECK_FALSE(parseChartDocument(
                    R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                    R"( "notes": [ { "position": "bad" } ] })")
                    .has_value());
    CHECK_FALSE(
        parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
            R"( "notes": [ { "position": "1:1", "string": 1, "fret": 0, "attack": "chug" } ] })")
            .has_value());
    CHECK_FALSE(
        parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
            R"( "notes": [ { "position": "1:1", "string": 1, "fret": 0, "bend": [[0, 1]] } ] })")
            .has_value());
    // A slide-out must carry a parseable end offset.
    CHECK_FALSE(parseChartDocument(
                    R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                    R"( "notes": [ { "position": "1:1", "string": 1, "fret": 3,)"
                    R"( "slideOut": { "fret": 15 } } ] })")
                    .has_value());
}

TEST_CASE("Chart rules reject structural violations", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    Chart unsorted = makeFullChart();
    std::swap(unsorted.notes[0], unsorted.notes[1]);
    const auto unsorted_result = validateChartRules(unsorted, tempo_map);
    REQUIRE_FALSE(unsorted_result.has_value());
    CHECK(unsorted_result.error().code == ChartErrorCode::UnsortedOrDuplicateNotes);

    Chart bad_string = makeFullChart();
    bad_string.notes[0].string = 7;
    const auto bad_string_result = validateChartRules(bad_string, tempo_map);
    REQUIRE_FALSE(bad_string_result.has_value());
    CHECK(bad_string_result.error().code == ChartErrorCode::InvalidNote);

    Chart bad_beat = makeFullChart();
    bad_beat.notes.back().position = GridPosition{.measure = 1, .beat = 5};
    const auto bad_beat_result = validateChartRules(bad_beat, tempo_map);
    REQUIRE_FALSE(bad_beat_result.has_value());
    CHECK(bad_beat_result.error().code == ChartErrorCode::InvalidNote);

    Chart slide_past_sustain = makeFullChart();
    slide_past_sustain.notes[2].slides.back().offset = Fraction{3};
    const auto slide_result = validateChartRules(slide_past_sustain, tempo_map);
    REQUIRE_FALSE(slide_result.has_value());
    CHECK(slide_result.error().code == ChartErrorCode::InvalidNotePayload);

    // A slide-out must end strictly after every curve waypoint.
    Chart trail_before_waypoint = makeFullChart();
    trail_before_waypoint.notes[2].slide_out = SlideOut{.offset = Fraction{1, 4}, .fret = 15};
    const auto trail_result = validateChartRules(trail_before_waypoint, tempo_map);
    REQUIRE_FALSE(trail_result.has_value());
    CHECK(trail_result.error().code == ChartErrorCode::InvalidNotePayload);

    // A curve waypoint may not sit on a later onset of its string — a glide ends the minimum
    // note distance before its re-picked landing, whose own head renders there.
    Chart waypoint_on_onset = makeFullChart();
    waypoint_on_onset.notes[5].sustain = Fraction{1, 3};
    waypoint_on_onset.notes[5].slides = {SlideWaypoint{.offset = Fraction{1, 3}, .fret = 5}};
    const auto coincident_result = validateChartRules(waypoint_on_onset, tempo_map);
    REQUIRE_FALSE(coincident_result.has_value());
    CHECK(coincident_result.error().code == ChartErrorCode::InvalidNotePayload);

    Chart bad_shape = makeFullChart();
    bad_shape.shapes[0].chord = 9;
    const auto bad_shape_result = validateChartRules(bad_shape, tempo_map);
    REQUIRE_FALSE(bad_shape_result.has_value());
    CHECK(bad_shape_result.error().code == ChartErrorCode::InvalidShape);

    Chart bad_template = makeFullChart();
    bad_template.templates[0].frets.pop_back();
    const auto bad_template_result = validateChartRules(bad_template, tempo_map);
    REQUIRE_FALSE(bad_template_result.has_value());
    CHECK(bad_template_result.error().code == ChartErrorCode::InvalidTemplate);

    // A harmonic touch position needs a harmonic and a real neck position.
    Chart touch_without_harmonic = makeFullChart();
    touch_without_harmonic.notes[0].touch = 3.2;
    const auto touch_result = validateChartRules(touch_without_harmonic, tempo_map);
    REQUIRE_FALSE(touch_result.has_value());
    CHECK(touch_result.error().code == ChartErrorCode::InvalidNote);

    // Cent offsets span a full octave because real bass arrangements charted on guitar strings
    // pitch down twelve hundred cents; anything beyond that is junk data.
    Chart octave_down = makeFullChart();
    octave_down.tuning.cent_offset = -1200.0;
    CHECK(validateChartRules(octave_down, tempo_map).has_value());

    Chart beyond_octave = makeFullChart();
    beyond_octave.tuning.cent_offset = -1201.0;
    const auto beyond_octave_result = validateChartRules(beyond_octave, tempo_map);
    REQUIRE_FALSE(beyond_octave_result.has_value());
    CHECK(beyond_octave_result.error().code == ChartErrorCode::InvalidTuning);
}

// The shared arrival rule: a strummed chord is a box; sequential arrival, or a posture string
// still ringing at the span start without an onset there, renders arpeggio-style. A posture
// string that is merely silent (a partial strum) or a ringing note outside the posture keeps
// the box.
TEST_CASE("Chart shape arrival classifies boxes and arpeggios", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.templates = {
        ChordTemplate{
            .name = "",
            .frets = {3, 6, 8, std::nullopt, std::nullopt, std::nullopt},
            .fingers = {
                std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt
            },
        },
    };
    // A sustained note on string 2 rings from 2:1 through 2:3; a two-string strum lands at 2:2.
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 6,
            .sustain = Fraction{2},
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2},
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2},
            .string = 3,
            .fret = 8,
            .bend = {},
            .slides = {},
        },
    };
    const ChartShape strum_under_ring{
        .position = GridPosition{.measure = 2, .beat = 2},
        .sustain = Fraction{1},
        .chord = 0,
    };

    // String 2's fret 6 is posture, un-restruck, and still ringing at the strum: arpeggio.
    CHECK(chartShapeArrivesAsArpeggio(chart, strum_under_ring, tempo_map));

    // A single onset at the span start is sequential arrival: arpeggio regardless of ringing.
    const ChartShape sequential{
        .position = GridPosition{.measure = 2, .beat = 1},
        .sustain = Fraction{1},
        .chord = 0,
    };
    CHECK(chartShapeArrivesAsArpeggio(chart, sequential, tempo_map));

    // With the ring ended before the strum, the posture string is merely silent — a partial
    // strum of the shape keeps the chord box.
    chart.notes[0].sustain = Fraction{1, 2};
    CHECK_FALSE(chartShapeArrivesAsArpeggio(chart, strum_under_ring, tempo_map));

    // A ringing note on a string outside the posture never flips the box: sustained content
    // under an unrelated chord is ordinary.
    chart.notes[0].sustain = Fraction{2};
    Chart no_ring_string = chart;
    no_ring_string.templates[0].frets = {
        3, std::nullopt, 8, std::nullopt, std::nullopt, std::nullopt
    };
    CHECK_FALSE(chartShapeArrivesAsArpeggio(no_ring_string, strum_under_ring, tempo_map));

    // A ring from an earlier chord member is still a ring: the re-strum picks around the held
    // string, so it is an arpeggio too (a tied passage with a hand move splits into two
    // arpeggio shapes, user rule 2026-07-22).
    Chart chord_sourced_ring = chart;
    chord_sourced_ring.notes.insert(
        chord_sourced_ring.notes.begin() + 1,
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 4,
            .fret = 5,
            .bend = {},
            .slides = {},
        });
    CHECK(chartShapeArrivesAsArpeggio(chord_sourced_ring, strum_under_ring, tempo_map));
}

} // namespace rock_hero::common::core
