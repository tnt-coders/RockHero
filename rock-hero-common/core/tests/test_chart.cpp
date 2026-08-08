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
            .attack = NoteAttack::Pinch,
            // Beyond the stop at fret 5, because nothing vibrates behind the fretting finger: this
            // is the octave node of the stopped string (5 + 12), the commonest pinch target.
            .harmonic_node = 17.0,
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
        // A down-then-up chained scrape, then an adjacent simple one: pick-slide notes carry
        // the traveled path in `slides` with the last offset equal to the sustain.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 5,
            .fret = 17,
            .sustain = Fraction{1},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            .slides =
                {
                    SlideWaypoint{.offset = Fraction{1, 2}, .fret = 5},
                    SlideWaypoint{.offset = Fraction{1}, .fret = 9},
                },
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 4,
            .fret = 3,
            .sustain = Fraction{1, 2},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            .slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 12}},
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

// The harmonic has no field of its own: a node asserts one and the attack says which hand damps
// it. These are the states that shape makes reachable, and the one it makes unreachable.
// Notation writes conventional labels, not measurements, so import snaps them onto the physics: a
// label even slightly off chokes a high harmonic instead of ringing it.
TEST_CASE("Chart harmonic nodes snap onto the physics", "[core][chart]")
{
    // The nut-side offsets guitarists name, against the labels notation prints for them.
    REQUIRE(harmonicPartialOffset(2).has_value());
    if (const auto octave = harmonicPartialOffset(2); octave.has_value())
    {
        CHECK(*octave == Catch::Approx(12.0));
    }
    if (const auto sixth = harmonicPartialOffset(6); sixth.has_value())
    {
        CHECK(*sixth == Catch::Approx(3.1564).margin(0.001)); // printed "3.2"
    }
    if (const auto eighth = harmonicPartialOffset(8); eighth.has_value())
    {
        CHECK(*eighth == Catch::Approx(2.3124).margin(0.001)); // printed "2.4" or "2.3"
    }
    // The fundamental has no node.
    CHECK_FALSE(harmonicPartialOffset(1).has_value());

    // Fret units are logarithmic, so a stop and a node offset simply add: the same offsets serve
    // every fretted position.
    CHECK(snapHarmonicNode(12.0, 0, g_max_snapped_partial) == Catch::Approx(12.0));
    CHECK(snapHarmonicNode(17.0, 5, g_max_snapped_partial) == Catch::Approx(17.0));

    // Conventional labels resolve to the partial the score meant, not to whatever node happens to
    // sit nearest. This is the guard on g_max_snapped_partial: at a cap of 16 the "2.4" below
    // resolves to the 15th partial instead of the 8th, because the nodes crowd tighter than the
    // label's own rounding error.
    CHECK(snapHarmonicNode(2.4, 0, g_max_snapped_partial) == Catch::Approx(2.3124).margin(0.001));
    CHECK(snapHarmonicNode(2.7, 0, g_max_snapped_partial) == Catch::Approx(2.6687).margin(0.001));
    CHECK(snapHarmonicNode(4.0, 0, g_max_snapped_partial) == Catch::Approx(3.8631).margin(0.001));
    // Bridge-side nodes are named too: 19 and 24 are the 3rd and 4th partials' later nodes.
    CHECK(snapHarmonicNode(19.0, 0, g_max_snapped_partial) == Catch::Approx(19.0196).margin(0.001));
    CHECK(snapHarmonicNode(24.0, 0, g_max_snapped_partial) == Catch::Approx(24.0));

    // A cap below 2 has no partials to search, so it yields the octave rather than nothing.
    CHECK(snapHarmonicNode(3.2, 0, 1) == Catch::Approx(12.0));
}

TEST_CASE("Chart harmonics are a node plus an attack", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    const auto note_at = [](const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = fret;
        return note;
    };
    const auto round_trip = [](const ChartNote& note) {
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note};
        const auto parsed = parseChartDocument(chartDocumentText(chart));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->notes.size() == 1);
        return parsed->notes[0];
    };

    SECTION("a bare node is a natural harmonic, damped on the fretboard")
    {
        ChartNote note = note_at(0);
        note.harmonic_node = 3.2;
        const ChartNote parsed = round_trip(note);
        CHECK(parsed == note);
        CHECK(isHarmonic(parsed.harmonic_node));
        CHECK(fretboardHarmonicNode(parsed.harmonic_node, parsed.attack).has_value());
    }

    SECTION("a pinch carries its node when one is known")
    {
        ChartNote note = note_at(5);
        note.attack = NoteAttack::Pinch;
        note.harmonic_node = 17.0;
        const ChartNote parsed = round_trip(note);
        CHECK(parsed == note);
        CHECK(isHarmonic(parsed.harmonic_node));
        // The thumb grazes over the body, so the node is not a neck position to draw at.
        CHECK_FALSE(fretboardHarmonicNode(parsed.harmonic_node, parsed.attack).has_value());
    }

    SECTION("a pinch with no node is REFUSED")
    {
        // A pinch is picking while damping a node, so one without a node is missing data rather than
        // a different technique — the overtone that squeals is determined by where the thumb lands.
        // Enforcing this is what lets isHarmonic be node presence alone.
        ChartNote note = note_at(5);
        note.attack = NoteAttack::Pinch;
        Chart pinch_chart;
        pinch_chart.tuning.strings = {"E2"};
        pinch_chart.notes = {note};
        const auto result = validateChartRules(pinch_chart, tempo_map);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ChartErrorCode::InvalidNote);
    }

    SECTION("a node past the bridge-side limit is refused, but a sub-fret-1 node is fine")
    {
        // Higher harmonics crowd toward the nut, so a node below fret 1 is legitimate; the bound
        // only refuses positions past the 16th harmonic's bridge-side node.
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note_at(0)};
        chart.notes[0].harmonic_node = 1.1;
        CHECK(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = g_max_harmonic_node + 0.1;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());
    }

    SECTION("a tapped node is a tap harmonic, and its damper IS on the fretboard")
    {
        // The picking hand taps, but it lands on the neck — so unlike a pinch this anchors at the
        // node. That distinction is why the predicate asks about the fretboard, not about a hand.
        ChartNote note = note_at(5);
        note.attack = NoteAttack::Tap;
        note.harmonic_node = 17.0;
        const ChartNote parsed = round_trip(note);
        CHECK(parsed == note);
        CHECK(fretboardHarmonicNode(parsed.harmonic_node, parsed.attack).has_value());
    }

    SECTION("the removed fields are refused rather than silently dropped")
    {
        // Loading an un-reimported package must fail loudly: ignoring these keys would drop every
        // harmonic in the chart without a word.
        CHECK_FALSE(parseChartDocument(
                        R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                        R"( "notes": [ { "position": "1:1", "string": 1, "fret": 0,)"
                        R"( "harmonic": "natural" } ] })")
                        .has_value());
        CHECK_FALSE(parseChartDocument(
                        R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                        R"( "notes": [ { "position": "1:1", "string": 1, "fret": 12,)"
                        R"( "touch": 12.0 } ] })")
                        .has_value());
    }
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

    // A harmonic node must name a real neck position. The companion case this once covered — a
    // node with no harmonic — is gone on purpose: the node IS the harmonic now, so there is no
    // second field left for it to disagree with and no way to build the state to reject.
    Chart node_off_the_neck = makeFullChart();
    node_off_the_neck.notes[0].harmonic_node = g_max_harmonic_node + 1.0;
    const auto node_result = validateChartRules(node_off_the_neck, tempo_map);
    REQUIRE_FALSE(node_result.has_value());
    CHECK(node_result.error().code == ChartErrorCode::InvalidNote);

    // A bare node with an ordinary attack is a natural harmonic and perfectly valid — the rule
    // above must reject the position, never the presence.
    Chart natural_harmonic = makeFullChart();
    natural_harmonic.notes[0].harmonic_node = 3.2;
    CHECK(validateChartRules(natural_harmonic, tempo_map).has_value());

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

// Pick-slide notes: no other techniques in a saved document (the writer omits the in-memory
// overrides), and a required always-traveling path ending exactly at the sustain.
TEST_CASE("Chart rules validate pick-slide notes", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // The chained scrape in the full fixture.
    constexpr std::size_t scrape = 7;

    const auto expect_invalid = [&tempo_map](const Chart& chart) {
        const auto result = validateChartRules(chart, tempo_map);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ChartErrorCode::InvalidPickSlide);
    };

    Chart carried_technique = makeFullChart();
    carried_technique.notes[scrape].tremolo = true;
    expect_invalid(carried_technique);

    Chart carried_mute = makeFullChart();
    carried_mute.notes[scrape].mute = NoteMute::Full;
    expect_invalid(carried_mute);

    Chart empty_path = makeFullChart();
    empty_path.notes[scrape].slides.clear();
    expect_invalid(empty_path);

    Chart ringing_past_path = makeFullChart();
    ringing_past_path.notes[scrape].sustain = Fraction{3, 2};
    expect_invalid(ringing_past_path);

    // The travel is the gesture: a path leg that starts where it ends has nothing to scrape,
    // unlike note slides, whose equal-fret segments are legitimate holds.
    Chart stationary_start = makeFullChart();
    stationary_start.notes[scrape].slides[0].fret = 17;
    expect_invalid(stationary_start);

    Chart stationary_leg = makeFullChart();
    stationary_leg.notes[scrape].slides[1].fret = 5;
    expect_invalid(stationary_leg);

    // The one carve-out in the waypoint-on-onset rule: a scrape TERMINAL legally lands exactly
    // on the silencing next onset — a 40-Q2-B truncation parks the sustain, and therefore the
    // terminal, right there — while an interior waypoint on a later onset stays rejected like
    // any glide waypoint.
    Chart terminal_on_onset;
    terminal_on_onset.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    terminal_on_onset.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 12,
            .sustain = Fraction{1, 2},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            .slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 4}},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}},
            .string = 1,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
    };
    CHECK(validateChartRules(terminal_on_onset, tempo_map).has_value());

    Chart interior_on_onset = terminal_on_onset;
    interior_on_onset.notes[0].sustain = Fraction{1};
    interior_on_onset.notes[0].slides = {
        SlideWaypoint{.offset = Fraction{1, 2}, .fret = 4},
        SlideWaypoint{.offset = Fraction{1}, .fret = 9},
    };
    const auto interior_result = validateChartRules(interior_on_onset, tempo_map);
    REQUIRE_FALSE(interior_result.has_value());
    CHECK(interior_result.error().code == ChartErrorCode::InvalidNotePayload);
}

// Latent techniques survive in memory for attack toggling but never reach the document: the
// writer omits them, so the saved note is clean and passes the rules.
TEST_CASE("Chart writer omits overridden techniques on pick-slide notes", "[core][chart]")
{
    Chart chart = makeFullChart();
    ChartNote& scrape = chart.notes[7];
    REQUIRE(scrape.attack == NoteAttack::PickSlide);
    scrape.tremolo = true;
    scrape.vibrato = true;
    scrape.mute = NoteMute::Full;

    const auto parsed = parseChartDocument(chartDocumentText(chart));
    REQUIRE(parsed.has_value());
    const ChartNote& saved = parsed->notes[7];
    CHECK(saved.attack == NoteAttack::PickSlide);
    CHECK_FALSE(saved.tremolo);
    CHECK_FALSE(saved.vibrato);
    CHECK(saved.mute == NoteMute::None);
    CHECK(validateChartRules(*parsed, makeTempoMap()).has_value());
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

    // A tapped note sounding within the span turns that box into a held arpeggio: the fretting
    // hand holds the shape while the right hand taps above it (held-chord-under-tap).
    Chart tapped_over_hold = chart; // the box state above (the ring ended before the strum)
    tapped_over_hold.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 15,
            .attack = NoteAttack::Tap,
            .bend = {},
            .slides = {},
        });
    CHECK(chartShapeArrivesAsArpeggio(tapped_over_hold, strum_under_ring, tempo_map));

    // A pick slide inside the span flips the box exactly like a tap: both are right-hand
    // onsets sounding over the held shape.
    Chart scraped_over_hold = chart;
    scraped_over_hold.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 17,
            .sustain = Fraction{1, 4},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            .slides = {SlideWaypoint{.offset = Fraction{1, 4}, .fret = 3}},
        });
    CHECK(chartShapeArrivesAsArpeggio(scraped_over_hold, strum_under_ring, tempo_map));

    // A tap OUTSIDE the span (after it ends) leaves the box a box.
    Chart tapped_after = chart;
    tapped_after.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 3, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 15,
            .attack = NoteAttack::Tap,
            .bend = {},
            .slides = {},
        });
    CHECK_FALSE(chartShapeArrivesAsArpeggio(tapped_after, strum_under_ring, tempo_map));

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
    // arpeggio shapes).
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
