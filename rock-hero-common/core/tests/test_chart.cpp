#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
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
            // Above the fixture's capo at 2: the capo floor binds postures like notes.
            .frets = {3, 5, 5, std::nullopt, std::nullopt, std::nullopt},
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
        // optional turnaround waypoints in `slides` and the required unpitched terminal in
        // `slide_out`, its offset exactly at the sustain.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 5,
            .fret = 17,
            .sustain = Fraction{1},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            .slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 5}},
            .slide_out = SlideOut{.offset = Fraction{1}, .fret = 9},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 4,
            .fret = 3,
            .sustain = Fraction{1, 2},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            .slides = {},
            .slide_out = SlideOut{.offset = Fraction{1, 2}, .fret = 12},
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
    // The exact labels: the octave and the 4th partial's bridge-side node are true nodes, so
    // snapping must return them untouched. Call sites add the stop themselves — fret units are
    // logarithmic, so a stop and an open-string offset simply add.
    CHECK_THAT(snapHarmonicNode(12.0, g_max_snapped_partial), Catch::Matchers::WithinULP(12.0, 0));
    CHECK_THAT(snapHarmonicNode(24.0, g_max_snapped_partial), Catch::Matchers::WithinULP(24.0, 0));

    // Conventional labels resolve to the partial the score meant, not to whatever node happens to
    // sit nearest. This is the guard on g_max_snapped_partial: at a cap of 16 the "2.4" below
    // resolves to the 15th partial instead of the 8th, because the nodes crowd tighter than the
    // label's own rounding error.
    CHECK(snapHarmonicNode(2.4, g_max_snapped_partial) == Catch::Approx(2.3124).margin(0.001));
    CHECK(snapHarmonicNode(2.7, g_max_snapped_partial) == Catch::Approx(2.6687).margin(0.001));
    CHECK(snapHarmonicNode(4.0, g_max_snapped_partial) == Catch::Approx(3.8631).margin(0.001));
    // Bridge-side nodes are named too: 19 is the 3rd partial's second node.
    CHECK(snapHarmonicNode(19.0, g_max_snapped_partial) == Catch::Approx(19.0196).margin(0.001));

    // A cap below 2 has no partials to search, so it yields the octave rather than nothing.
    CHECK_THAT(snapHarmonicNode(3.2, 1), Catch::Matchers::WithinULP(12.0, 0));

    // Which fret the FRETTING hand occupies. A natural harmonic has no stop of its own, so the hand
    // is at the node; fret N spans wire N-1 to wire N, making that fret ceil(node) — NOT round and
    // NOT floor.
    const auto natural = [](const double node) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = 0;
        note.harmonic_node = node;
        return note;
    };
    CHECK(fretFor(natural(12.0)) == 12);
    CHECK(fretFor(natural(2.669)) == 3); // written "2.7"; lies in fret 3's span
    CHECK(fretFor(natural(3.156)) == 4); // written "3.2"; fret 4's span — round would say 3
    CHECK(fretFor(natural(7.020)) == 8); // just past wire 7 — round would say 7, putting it outside
    CHECK(fretFor(natural(4.980)) == 5);
    CHECK(fretFor(natural(24.0)) == 24);

    // A pinch and a two-hand tap both have a real stop, and their node is the other hand's, so the
    // fretting hand stays on the fret.
    ChartNote pinch = natural(29.0);
    pinch.fret = 5;
    pinch.attack = NoteAttack::Pinch;
    CHECK(fretFor(pinch) == 5);
    ChartNote tapped = natural(17.0);
    tapped.fret = 5;
    tapped.attack = NoteAttack::Tap;
    CHECK(fretFor(tapped) == 5);
    // A picked harmonic over a real stop — the harp / artificial-harmonic family — holds that
    // stop with the fretting hand while the picking hand damps the node, so the hand is at the
    // fret, not twelve frets up at the node.
    ChartNote artificial = natural(17.0);
    artificial.fret = 5;
    CHECK(fretFor(artificial) == 5);
    // A hammer harmonic IS the fretting hand rapping the node.
    ChartNote hammered = natural(12.0);
    hammered.attack = NoteAttack::Hammer;
    CHECK(fretFor(hammered) == 12);
    // An ordinary note is just its fret.
    ChartNote plain = natural(0.0);
    plain.harmonic_node.reset();
    plain.fret = 7;
    CHECK(fretFor(plain) == 7);

    // The property that makes ceil the only safe choice: a window told about fret H is only
    // guaranteed to cover fret units [H-1, H], so every node must fall inside its own fret's span.
    for (int partial = 2; partial <= g_max_snapped_partial; ++partial)
    {
        for (int index = 1; index < partial; ++index)
        {
            const double node =
                12.0 *
                std::log2(static_cast<double>(partial) / static_cast<double>(partial - index));
            const int fret = fretFor(natural(node));
            CHECK(static_cast<double>(fret - 1) <= node);
            CHECK(node <= static_cast<double>(fret));
        }
    }
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
        CHECK(parsed.harmonic_node.has_value());
        CHECK(nodeIsOnNeck(parsed.attack));
    }

    SECTION("a pinch carries its node when one is known")
    {
        ChartNote note = note_at(5);
        note.attack = NoteAttack::Pinch;
        note.harmonic_node = 17.0;
        const ChartNote parsed = round_trip(note);
        CHECK(parsed == note);
        CHECK(parsed.harmonic_node.has_value());
        // The thumb grazes over the body, so the node is not a neck position to draw at.
        CHECK_FALSE(nodeIsOnNeck(parsed.attack));
    }

    SECTION("a pinch with no node is REFUSED")
    {
        // A pinch is picking while damping a node, so one without a node is missing data rather
        // than a different technique — the overtone that squeals is set by where the thumb lands.
        // Enforcing this is what lets node presence alone assert the harmonic.
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
        // Higher harmonics crowd toward the nut, so a node below fret 1 is legitimate; the
        // universal bound only refuses positions past the 16th harmonic's bridge-side node — and
        // it is inclusive, since that node is itself real. The far reaches are probed through a
        // pinch, whose thumb damps off the neck: a fret-hand harmonic hits the neck-end bound
        // first (the section below).
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note_at(0)};
        chart.notes[0].harmonic_node = 1.1;
        CHECK(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].attack = NoteAttack::Pinch;
        chart.notes[0].harmonic_node = g_max_harmonic_node;
        CHECK(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = g_max_harmonic_node + 0.1;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = 0.0;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());
    }

    SECTION("a node at or behind the stop is refused")
    {
        // A node lies on the speaking length: nothing vibrates at or behind the stop, so the
        // comparison is strict — a node AT the stop is the stop.
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note_at(3)};
        chart.notes[0].harmonic_node = 3.0;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = 2.7;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = 3.2;
        CHECK(validateChartRules(chart, tempo_map).has_value());
    }

    SECTION("on a capo'd string, fret 0 speaks from the capo and the node must lie beyond it")
    {
        // The 0-means-open convention: the capo'd open string stores fret 0, so the physical
        // stop the node must clear is the capo, not the stored fret.
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.tuning.capo = 2;
        chart.notes = {note_at(0)};
        chart.notes[0].harmonic_node = 1.5;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = 2.0;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = 2.1;
        CHECK(validateChartRules(chart, tempo_map).has_value());
    }

    SECTION("a fret-hand harmonic's node must lie on the neck; off-neck damping is exempt")
    {
        // The fretting hand touches a fret-hand harmonic's node, and a finger on the fretboard
        // cannot be past the last fret — which is also what keeps the derived hand window inside
        // g_max_fret. A pinch's thumb grazes over the body, and a tapped node belongs to the
        // picking hand, so both escape the bound (only the universal 48 limit applies to them).
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note_at(0)};
        chart.notes[0].harmonic_node = static_cast<double>(g_max_fret) + 1.0;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = static_cast<double>(g_max_fret);
        CHECK(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = static_cast<double>(g_max_fret) + 1.0;
        chart.notes[0].attack = NoteAttack::Pinch;
        CHECK(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].attack = NoteAttack::Tap;
        CHECK(validateChartRules(chart, tempo_map).has_value());
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
        CHECK(nodeIsOnNeck(parsed.attack));
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

// The signed technique matrix, enforced: every forbidden combination refuses, and the allowed
// staples that sit next to a forbid stay legal. validateChartNotes is the one authority; the
// editor planners gate candidates through the same checks the reader applies.
TEST_CASE("Chart rules enforce the technique compatibility matrix", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    const auto make_note = [](const int beat, const int string, const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = beat};
        note.string = string;
        note.fret = fret;
        return note;
    };
    const auto validate = [&tempo_map](const std::vector<ChartNote>& notes, const int capo = 0) {
        ChartTuning tuning;
        tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        tuning.capo = capo;
        return validateChartNotes(notes, tuning, tempo_map);
    };

    SECTION("a hammer or tap needs a place to strike")
    {
        ChartNote hammer = make_note(1, 1, 0);
        hammer.attack = NoteAttack::Hammer;
        CHECK_FALSE(validate({hammer}).has_value());

        ChartNote tap = make_note(1, 1, 0);
        tap.attack = NoteAttack::Tap;
        CHECK_FALSE(validate({tap}).has_value());

        // The open-string tap harmonic strikes the node itself, so a node satisfies the rule.
        tap.harmonic_node = 12.0;
        CHECK(validate({tap}).has_value());

        hammer.fret = 5;
        CHECK(validate({hammer}).has_value());
    }

    SECTION("a full mute excludes the pitch-valued payloads and keeps the positions")
    {
        ChartNote dead = make_note(1, 1, 5);
        dead.mute = NoteMute::Full;
        dead.sustain = Fraction{1};
        CHECK(validate({dead}).has_value());

        ChartNote muted_harmonic = dead;
        muted_harmonic.harmonic_node = 17.0;
        CHECK_FALSE(validate({muted_harmonic}).has_value());

        ChartNote muted_bend = dead;
        muted_bend.bend = {BendPoint{.offset = Fraction{0}, .semitones = 1.0}};
        CHECK_FALSE(validate({muted_bend}).has_value());

        ChartNote muted_vibrato = dead;
        muted_vibrato.vibrato = true;
        CHECK_FALSE(validate({muted_vibrato}).has_value());

        // Positions survive the same test: the dragged muted slide is real music.
        ChartNote muted_slide = dead;
        muted_slide.slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 9}};
        CHECK(validate({muted_slide}).has_value());
    }

    SECTION("a pull-off can neither sound nor release a harmonic")
    {
        const ChartNote source = make_note(1, 1, 9);
        ChartNote pull = make_note(2, 1, 5);
        pull.attack = NoteAttack::Pull;
        CHECK(validate({source, pull}).has_value());

        ChartNote pulled_harmonic = pull;
        pulled_harmonic.harmonic_node = 17.0;
        CHECK_FALSE(validate({source, pulled_harmonic}).has_value());

        ChartNote harmonic_source = make_note(1, 1, 0);
        harmonic_source.harmonic_node = 12.0;
        CHECK_FALSE(validate({harmonic_source, pull}).has_value());
    }

    SECTION("a pull-off needs a higher released fret on its string")
    {
        ChartNote pull = make_note(2, 1, 5);
        pull.attack = NoteAttack::Pull;
        CHECK_FALSE(validate({pull}).has_value());

        const ChartNote equal_source = make_note(1, 1, 5);
        CHECK_FALSE(validate({equal_source, pull}).has_value());

        // The released fret is where the finger ENDS: a 3->7 glide hands over 7, justifying a
        // pull to 5 that the onset frets alone would refuse.
        ChartNote gliding_source = make_note(1, 1, 3);
        gliding_source.sustain = Fraction{1, 2};
        gliding_source.slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 7}};
        CHECK(validate({gliding_source, pull}).has_value());

        // A scrape predecessor is valid data: its released fret is the slide-out's end.
        ChartNote scrape_source = make_note(1, 1, 12);
        scrape_source.sustain = Fraction{1, 2};
        scrape_source.attack = NoteAttack::PickSlide;
        scrape_source.slide_out = SlideOut{.offset = Fraction{1, 2}, .fret = 7};
        CHECK(validate({scrape_source, pull}).has_value());
    }

    SECTION("a tap harmonic cannot be tremolo picked; a picked one over a stop can")
    {
        ChartNote tap_harmonic = make_note(1, 1, 5);
        tap_harmonic.attack = NoteAttack::Tap;
        tap_harmonic.harmonic_node = 17.0;
        tap_harmonic.tremolo = true;
        CHECK_FALSE(validate({tap_harmonic}).has_value());

        // The artificial-harmonic family keeps a finger on the node, so re-picking works.
        ChartNote artificial = make_note(1, 1, 5);
        artificial.harmonic_node = 17.0;
        artificial.tremolo = true;
        CHECK(validate({artificial}).has_value());
    }

    SECTION("a fret-hand harmonic cannot slide, bend, or vibrato; one over a stop can")
    {
        ChartNote natural = make_note(1, 1, 0);
        natural.harmonic_node = 12.0;
        natural.sustain = Fraction{1};
        CHECK(validate({natural}).has_value());

        ChartNote sliding = natural;
        sliding.slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 14}};
        CHECK_FALSE(validate({sliding}).has_value());

        ChartNote trailing = natural;
        trailing.slide_out = SlideOut{.offset = Fraction{1, 2}, .fret = 9};
        CHECK_FALSE(validate({trailing}).has_value());

        ChartNote bending = natural;
        bending.bend = {BendPoint{.offset = Fraction{0}, .semitones = 1.0}};
        CHECK_FALSE(validate({bending}).has_value());

        ChartNote oscillating = natural;
        oscillating.vibrato = true;
        CHECK_FALSE(validate({oscillating}).has_value());

        // A harmonic over a real stop is the picking-hand-damped family: the fretting hand is
        // pressing, so bending it is ordinary work.
        ChartNote fretted = make_note(1, 1, 5);
        fretted.harmonic_node = 17.0;
        fretted.sustain = Fraction{1};
        fretted.bend = {BendPoint{.offset = Fraction{0}, .semitones = 1.0}};
        CHECK(validate({fretted}).has_value());
    }

    SECTION("the capo floor binds notes")
    {
        CHECK(validate({make_note(1, 1, 5)}, 3).has_value());
        CHECK(validate({make_note(1, 1, 0)}, 3).has_value());
        CHECK_FALSE(validate({make_note(1, 1, 2)}, 3).has_value());
        CHECK_FALSE(validate({make_note(1, 1, 3)}, 3).has_value());
    }

    SECTION("the capo floor binds pitched glide waypoints but not scrape travel")
    {
        ChartNote glide = make_note(1, 1, 5);
        glide.sustain = Fraction{1};
        glide.slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 4}};
        CHECK(validate({glide}, 3).has_value());

        glide.slides.front().fret = 2;
        CHECK_FALSE(validate({glide}, 3).has_value());

        // A scrape's turnaround is unpitched pick travel and may dip below the capo.
        ChartNote scrape = make_note(1, 1, 5);
        scrape.attack = NoteAttack::PickSlide;
        scrape.sustain = Fraction{1};
        scrape.slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 1}};
        scrape.slide_out = SlideOut{.offset = Fraction{1}, .fret = 6};
        CHECK(validate({scrape}, 3).has_value());
    }
}

// Pick-slide notes: no pitched techniques in a saved document (the writer omits the in-memory
// overrides; accent is a scrape's own technique), the required unpitched slide-out terminal
// exactly at the sustain, and an always-traveling path.
TEST_CASE("Chart rules validate pick-slide notes", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // The chained scrape in the full fixture: fret 17, one turnaround waypoint, slide-out.
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

    // An accented scrape is legal — an aggressively played pick slide, the scrape's own
    // technique rather than an override.
    Chart accented = makeFullChart();
    accented.notes[scrape].accent = true;
    CHECK(validateChartRules(accented, tempo_map).has_value());

    Chart missing_terminal = makeFullChart();
    missing_terminal.notes[scrape].slide_out.reset();
    expect_invalid(missing_terminal);

    // Turnaround waypoints are optional: a plain start-to-terminal scrape is the common case.
    Chart no_turnarounds = makeFullChart();
    no_turnarounds.notes[scrape].slides.clear();
    CHECK(validateChartRules(no_turnarounds, tempo_map).has_value());

    Chart ringing_past_path = makeFullChart();
    ringing_past_path.notes[scrape].sustain = Fraction{3, 2};
    expect_invalid(ringing_past_path);

    // The travel is the gesture: a path leg that starts where it ends has nothing to scrape,
    // unlike note slides, whose equal-fret segments are legitimate holds.
    Chart stationary_start = makeFullChart();
    stationary_start.notes[scrape].slides[0].fret = 17;
    expect_invalid(stationary_start);

    Chart stationary_terminal = makeFullChart();
    stationary_terminal.notes[scrape].slide_out = SlideOut{.offset = Fraction{1}, .fret = 5};
    expect_invalid(stationary_terminal);

    // A scrape's slide-out legally lands exactly on the silencing next onset — a 40-Q2-B
    // truncation parks the sustain, and therefore the terminal, right there. That needs no
    // carve-out now: the waypoint-on-onset rule never sees a slide-out. An interior turnaround
    // on a later onset stays rejected like any glide waypoint.
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
            .slides = {},
            .slide_out = SlideOut{.offset = Fraction{1, 2}, .fret = 4},
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
    interior_on_onset.notes[0].slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 4}};
    interior_on_onset.notes[0].slide_out = SlideOut{.offset = Fraction{1}, .fret = 9};
    const auto interior_result = validateChartRules(interior_on_onset, tempo_map);
    REQUIRE_FALSE(interior_result.has_value());
    CHECK(interior_result.error().code == ChartErrorCode::InvalidNotePayload);
}

// Latent pitched techniques survive in memory for attack toggling but never reach the document:
// the writer omits them, so the saved note is clean and passes the rules. Accent and the slide
// payloads are the scrape's own data and round-trip.
TEST_CASE("Chart writer omits overridden techniques on pick-slide notes", "[core][chart]")
{
    Chart chart = makeFullChart();
    ChartNote& scrape = chart.notes[7];
    REQUIRE(scrape.attack == NoteAttack::PickSlide);
    scrape.tremolo = true;
    scrape.vibrato = true;
    scrape.mute = NoteMute::Full;
    scrape.accent = true;

    const auto parsed = parseChartDocument(chartDocumentText(chart));
    REQUIRE(parsed.has_value());
    const ChartNote& saved = parsed->notes[7];
    CHECK(saved.attack == NoteAttack::PickSlide);
    CHECK_FALSE(saved.tremolo);
    CHECK_FALSE(saved.vibrato);
    CHECK(saved.mute == NoteMute::None);
    CHECK(saved.accent);
    REQUIRE(saved.slide_out.has_value());
    if (saved.slide_out.has_value())
    {
        CHECK(saved.slide_out->fret == 9);
    }
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
            .slides = {},
            .slide_out = SlideOut{.offset = Fraction{1, 4}, .fret = 3},
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
