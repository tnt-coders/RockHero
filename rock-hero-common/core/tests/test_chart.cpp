#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <compare>
#include <limits>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// A 4/4 default map long enough that measures one through four are valid grid.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// The ring a fixture note starts with when the case is not about its duration: an eighth of a
// beat. Positive, as every stored ring must be (a zero one is refused); under the kept-sustain
// bound, so it presents no tail and earns no group one; and shorter than every gap these fixtures
// use, so it justifies no legato claim. A case that turns on the duration states its own.
constexpr Fraction g_fixture_ring{1, 8};

// One chart exercising every construct the format defines.
[[nodiscard]] Chart makeFullChart()
{
    Chart chart;
    chart.tuning = ChartTuning{
        .strings = {"C2", "G2", "C3", "F3", "A3", "D4"},
        .capo = 2,
        .cent_offset = -6.5,
    };
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 0,
            .sustain = g_fixture_ring,
            .palm_mute = true,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}},
            .string = 3,
            .fret = 7,
            // The left-hand tap: a LOCAL claim, so it needs no predecessor and no write can settle
            // it away — which is what makes it safe in a fixture asserted round-trip exact.
            .sustain = g_fixture_ring,
            .attack = NoteAttack::LeftTap,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 2},
            .string = 4,
            .fret = 12,
            .sustain = Fraction{1, 2},
            .attack = NoteAttack::Tap,
            .bend = 0.0,
            .keyframes =
                {Keyframe{.offset = Fraction{1, 4}, .fret = 13},
                 // The slide-out: a fret stated exactly at the ring's end, where the hand leaves
                 // toward it rather than sounding it.
                 Keyframe{.offset = Fraction{1, 2}, .fret = 15}},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{4},
            .vibrato = VibratoState::Narrow,
            // The bend channel: an unbent onset, then a curl, a full step, and a slide-out, each on
            // its own keyframe. Nothing states a fret, so the note never travels — a compound bend
            // held at one stop, which is exactly the shape a fret-less keyframe exists to write.
            .bend = 0.0,
            .keyframes =
                {
                    Keyframe{.offset = Fraction{1, 2}, .bend = 0.5},
                    Keyframe{.offset = Fraction{2}, .bend = 2.0},
                    Keyframe{.offset = Fraction{4}, .bend = 0.0},
                },
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{1},
            .attack = NoteAttack::Pinch,
            // Beyond the stop at fret 5, because nothing vibrates behind the fretting finger. The
            // 5th partial's node rather than the octave's, COMPUTED rather than typed: it is what
            // import actually stores (a node is 12*log2(partial) above the stop), and no rounded
            // spelling of it survives a round trip. A fixture built only from values a writer
            // cannot damage — 17.0, 0.5, 2.0 — would let the round-trip assertion below pass while
            // the writer silently truncates every real measurement.
            .harmonic_node = 5.0 + (12.0 * std::log2(5.0 / 4.0)),
            .tremolo = true,
            .emphasis = NoteEmphasis::Accent,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2},
            .string = 6,
            .fret = 3,
            .sustain = Fraction{1, 3},
            .attack = NoteAttack::Slap,
            .bend = 0.0,
            // A shift-slide glide, written the way every pitched arrival is: the stop sits one
            // margin INSIDE the ring — at the end it would be the slide-out instead — and the ring
            // runs on to the re-picked landing (the 3:2+1/3 note below).
            .keyframes = {Keyframe{.offset = Fraction{1, 12}, .fret = 5}},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2, .offset = Fraction{1, 3}},
            .string = 6,
            .fret = 5,
            .sustain = g_fixture_ring,
            .attack = NoteAttack::Pop,
            // Both mutes at once, the state one exclusive mute axis could not write down: the
            // palm is on the strings AND this string is deadened. It rides the full-chart fixture
            // so every round-trip, validation, and resolution case sees the combination.
            .palm_mute = true,
            .dead = true,
            .bend = 0.0,
            .keyframes = {},
        },
        // A down-then-up chained scrape, then an adjacent simple one: pick-slide notes carry
        // optional turnaround keyframes and the required unpitched terminal as the SLIDE-OUT, the
        // keyframe whose offset is exactly the sustain.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 5,
            .fret = 17,
            .sustain = Fraction{1},
            .attack = NoteAttack::PickSlide,
            .bend = 0.0,
            .keyframes =
                {Keyframe{.offset = Fraction{1, 2}, .fret = 5},
                 Keyframe{.offset = Fraction{1}, .fret = 9}},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 4,
            .fret = 3,
            .sustain = Fraction{1, 2},
            .attack = NoteAttack::PickSlide,
            .bend = 0.0,
            .keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 12}},
        },
        // A connection claim with the note that justifies it: the predecessor rings exactly to the
        // claim's onset, which is what the hold test asks (strict adjacency — and the same-string
        // clamp makes that the longest legal ring). Written last so the pair stays round-trip
        // exact — a claim nothing justified would leave as a plain pick.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3},
            .string = 6,
            .fret = 5,
            .sustain = Fraction{1, 2},
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3, .offset = Fraction{1, 2}},
            .string = 6,
            .fret = 7,
            .sustain = g_fixture_ring,
            .attack = NoteAttack::Legato,
            .bend = 0.0,
            .keyframes = {},
        },
    };
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 1, .beat = 1}, .fret = 5},
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 7},
    };
    return chart;
}

// Classifies the span the derivation produces at `position`, end to end from one stream.
//
// DERIVED rather than handed in. Three of the four arrival triggers are facts the WALK records on
// the span — a carry into its start and a partial sounding inside it are one comparison — so a case
// stating its own span and posture would be stating the very answer it asks about. Each case below
// therefore varies the NOTES, which is the only authored input the class is a function of.
[[nodiscard]] bool arrivesAsArpeggio(
    const std::vector<ChartNote>& notes, const GridPosition& position, const TempoMap& tempo_map)
{
    const ChartResolutions resolved = chartResolutions(notes, tempo_map);
    const std::vector<bool> arrivals = chartShapeArrivals(notes, resolved.shapes, tempo_map);
    REQUIRE(arrivals.size() == resolved.shapes.size());
    // The span COVERING the slot, not the one starting exactly on it. THE DATING RULE puts a span's
    // FRONT at its earliest uncovered member onset, so a strum that picks around a still-ringing
    // note is inside a statement that began at that note — asking for a span starting on the strum
    // would ask for the slot the walk NOTICED the shape at, which is not a fact the model
    // publishes. Spans never overlap, so "covers" names exactly one.
    std::optional<bool> found;
    for (std::size_t index = 0; index < resolved.shapes.size(); ++index)
    {
        const ChartShape& shape = resolved.shapes[index];
        const Fraction start = beatDistance(tempo_map, GridPosition{}, shape.position);
        const Fraction at = beatDistance(tempo_map, GridPosition{}, position);
        if (!(at < start) && (at < start + shape.sustain || at == start))
        {
            found = arrivals[index];
        }
    }
    // A case asking about a span the derivation never made would pass for the wrong reason.
    REQUIRE(found.has_value());
    return found.has_value() && *found;
}

// A four-beat fret-5 hold on the low E opening at `onset`'s width, for the per-leg vibrato cases:
// long enough that a keyframe one, two or three beats in lies strictly inside the ring.
[[nodiscard]] ChartNote vibratoLegNote(
    const VibratoState onset, const std::vector<Keyframe>& keyframes)
{
    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = 1};
    note.string = 1;
    note.fret = 5;
    note.sustain = Fraction{4};
    note.vibrato = onset;
    note.keyframes = keyframes;
    return note;
}

// Whether the whole-chart gate accepts `note` alone on a one-string chart.
[[nodiscard]] bool acceptedAlone(const ChartNote& note)
{
    Chart chart;
    chart.tuning.strings = {"E2"};
    chart.notes = {note};
    return validateChartRules(chart, makeTempoMap()).has_value();
}

} // namespace

// Verifies grid position tokens parse whole and sub-beat spellings and reformat canonically.
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

// Verifies beat fraction tokens parse whole and fractional spellings, refuse malformed ones, and
// reformat canonically.
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

// Verifies the fixture holding every construct the format defines survives a write and read
// unchanged, writes each note's ring, and satisfies the structural rules.
TEST_CASE("Chart document round-trips every construct", "[core][chart]")
{
    const Chart chart = makeFullChart();

    const std::string text = chartDocumentText(chart, makeTempoMap());
    const auto parsed = parseChartDocument(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == chart);

    // Every note's ring is written, with no short form to elide into: the reader requires the key,
    // so a note without it is a document nothing can read. Counting them is what would catch an
    // "omit when it equals the default" optimization creeping back in.
    std::size_t written_rings = 0;
    for (std::size_t at = text.find("\"sustain\""); at != std::string::npos;
         at = text.find("\"sustain\"", at + 1))
    {
        ++written_rings;
    }
    // Exactly one per note: every note rings, and nothing else in the document carries a sustain,
    // since the hand-shape spans are derived rather than written.
    CHECK(written_rings == chart.notes.size());

    // The full fixture also satisfies the structural rules.
    CHECK(validateChartRules(chart, makeTempoMap()).has_value());
}

// The harmonic has no field of its own: a node asserts one and the attack says which hand damps
// it. These are the states that shape makes reachable, and the one it makes unreachable.
// Notation writes conventional labels, not measurements, so import snaps them onto the physics: a
// label even slightly off chokes a high harmonic instead of ringing it.
TEST_CASE("Chart harmonic nodes snap onto the physics", "[core][chart]")
{
    // The one resolution both producers read: what a label MAY mean, and which of those meanings
    // is nearest. Call sites add the stop themselves — fret units are logarithmic, so a stop and
    // an open-string offset simply add.
    //
    // The exact labels are true nodes and name exactly one each, carrying the LOWEST partial that
    // sounds there: 12 is a node of the 2nd, 4th, 6th and 8th, and the 2nd is what rings.
    const std::vector<HarmonicNodeCandidate> octave =
        harmonicNodeCandidates(12.0, g_max_snapped_partial);
    REQUIRE(octave.size() == 1);
    CHECK_THAT(octave.front().position, Catch::Matchers::WithinULP(12.0, 0));
    CHECK(octave.front().partial == 2);

    // Conventional labels resolve to the partial the score meant, not to whatever node happens to
    // sit nearest. This is the guard on g_max_snapped_partial: at a cap of 16 the "2.4" below
    // resolves to the 15th partial instead of the 8th, because the nodes crowd tighter than the
    // label's own rounding error.
    const auto nearest_node = [](const double label) {
        const std::vector<HarmonicNodeCandidate> candidates =
            harmonicNodeCandidates(label, g_max_snapped_partial);
        REQUIRE_FALSE(candidates.empty());
        return candidates[nearestHarmonicNode(candidates, label)].position;
    };
    CHECK(nearest_node(2.4) == Catch::Approx(2.3124).margin(0.001));
    CHECK(nearest_node(2.7) == Catch::Approx(2.6687).margin(0.001));
    CHECK(nearest_node(4.0) == Catch::Approx(3.8631).margin(0.001));
    // Bridge-side nodes are named too: 19 is the 3rd partial's second node, and 24 the 4th's third.
    CHECK(nearest_node(19.0) == Catch::Approx(19.0196).margin(0.001));
    CHECK(nearest_node(24.0) == Catch::Approx(24.0).margin(0.001));
    // 7 is the commonest harmonic on the instrument and names ONE node, so a charter typing it
    // never has to choose.
    CHECK(nearest_node(7.0) == Catch::Approx(7.0196).margin(0.001));

    // THE ONE AMBIGUOUS LABEL under this cap. "3" sits 0.331 from the 7th partial's 2.669 and
    // 0.156 from the 6th's 3.156, and both are inside the label window, so import has to pick the
    // nearest. Ordered by position, which is why the nearest here is the second row.
    const std::vector<HarmonicNodeCandidate> three =
        harmonicNodeCandidates(3.0, g_max_snapped_partial);
    REQUIRE(three.size() == 2);
    CHECK(three[0].position == Catch::Approx(2.6687).margin(0.001));
    CHECK(three[0].partial == 7);
    CHECK(three[1].position == Catch::Approx(3.1564).margin(0.001));
    CHECK(three[1].partial == 6);
    CHECK(nearestHarmonicNode(three, 3.0) == 1);

    // The DEAD keys: an integer fret with no harmonic within half a fret names nothing at all, and
    // resolving it anyway would move the touch a whole fret and sound a different partial.
    for (const double label : {1.0, 11.0, 13.0})
    {
        INFO("label " << label);
        CHECK(harmonicNodeCandidates(label, g_max_snapped_partial).empty());
    }

    // A cap below 2 has no partials to search at all.
    CHECK(harmonicNodeCandidates(3.2, 1).empty());

    // The one node-label authority (shared by the 2D head text and the 3D floor numbers): one
    // decimal, dropped when whole, rounded in integer tenths so the whole test and the printed
    // tenth cannot disagree.
    CHECK(harmonicNodeText(2.311741) == "2.3");
    CHECK(harmonicNodeText(3.155814) == "3.2");
    CHECK(harmonicNodeText(4.98) == "5");
    CHECK(harmonicNodeText(12.0) == "12");
    CHECK(harmonicNodeText(19.0196) == "19");

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
    // A left-hand tap harmonic IS the fretting hand rapping the node.
    ChartNote hammered = natural(12.0);
    hammered.attack = NoteAttack::LeftTap;
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

// THE CAP IS THE CALLER'S, THE ORDER IS NOT. The editor's verb resolves the same label against
// g_max_harmonic_partial — every partial validation accepts — so "5" names three nodes where
// import's narrower cap names one. This function orders them by POSITION whatever the cap; the
// partial-ascending order the picker lists is the editor operand's own sort, stated there.
TEST_CASE("Chart harmonic nodes widen under the editor's partial bound", "[core][chart]")
{
    const std::vector<HarmonicNodeCandidate> label_five =
        harmonicNodeCandidates(5.0, g_max_harmonic_partial);
    REQUIRE(label_five.size() == 3);
    CHECK_THAT(label_five[0].position, Catch::Matchers::WithinAbs(4.5421, 0.001));
    CHECK(label_five[0].partial == 13);
    CHECK_THAT(label_five[1].position, Catch::Matchers::WithinAbs(4.9804, 0.001));
    CHECK(label_five[1].partial == 4);
    CHECK_THAT(label_five[2].position, Catch::Matchers::WithinAbs(5.3695, 0.001));
    CHECK(label_five[2].partial == 15);
}

// THE GRIP STOP: a grip is a PLACE on the fret axis — a fret pressed, the open string, or a
// harmonic node touched — and NODE 5 IS NOT FRET 5. The type is the pair the chart already spells
// on a note, built only by its two factories; its comparisons stay defaulted (the float is reached
// through std::optional<double>, the case the conventions name safe), so the suite instantiates
// them here on purpose — a defaulted comparison is only defined once odr-used, and a float-equal
// regression could otherwise hide until a line nobody edited.
TEST_CASE("A grip stop is a place on the fret axis, not a fret number", "[core][chart]")
{
    SECTION("node, fret and open are three different places")
    {
        // A finger on the fifth wire is not a finger in the fifth slot.
        CHECK(frettedStop(5) != nodeStop(5.0));
        // And not the open string it shares a fret number with: the finger IS on the string.
        CHECK(nodeStop(12.0) != frettedStop(0));
        CHECK(nodeStop(12.0) == nodeStop(12.0));
        CHECK(frettedStop(0) == ChartStop{});
        CHECK(frettedStop(7) == frettedStop(7));
        CHECK(nodeStop(7.01955) != nodeStop(7.0));
    }

    SECTION("the hand's fret is the one ceil law, and the label is the one label authority")
    {
        CHECK(handFretOf(nodeStop(12.0)) == 12);
        CHECK(handFretOf(nodeStop(2.669)) == 3);
        CHECK(handFretOf(nodeStop(3.156)) == 4);
        CHECK(handFretOf(nodeStop(7.01955)) == 8);
        CHECK(handFretOf(frettedStop(0)) == 0);
        CHECK(handFretOf(frettedStop(9)) == 9);

        CHECK(chartStopText(nodeStop(7.01955)) == "7");
        CHECK(chartStopText(nodeStop(2.669)) == "2.7");
        CHECK(chartStopText(frettedStop(12)) == "12");
        CHECK(chartStopText(frettedStop(0)) == "0");
    }

    SECTION("the fretting hand's stop, per harmonic family")
    {
        const auto natural = [](const double node) {
            ChartNote note;
            note.position = GridPosition{.measure = 1, .beat = 1};
            note.string = 1;
            note.fret = 0;
            note.harmonic_node = node;
            return note;
        };
        // A natural harmonic's finger is on its node and presses nothing.
        CHECK(frettingStopAt(natural(12.0), 0) == nodeStop(12.0));
        // A left-hand tap harmonic IS the fretting hand rapping the node.
        ChartNote hammered = natural(12.0);
        hammered.attack = NoteAttack::LeftTap;
        CHECK(frettingStopAt(hammered, 0) == nodeStop(12.0));
        // A two-hand tap harmonic's node belongs to the picking hand: the fretting hand is on the
        // note's own stop, and what it SOUNDS is a different question (\ref soundingStopAt).
        ChartNote tapped = natural(17.0);
        tapped.fret = 5;
        tapped.attack = NoteAttack::Tap;
        CHECK(frettingStopAt(tapped, 5) == frettedStop(5));
        CHECK(
            soundingStopAt(tapped.harmonic_node, tapped.attack, tapped.fret, 5) == nodeStop(17.0));
        // A pinch's node is off the neck: the hand stays on the stop, and so does the sound.
        ChartNote pinch = natural(29.0);
        pinch.fret = 5;
        pinch.attack = NoteAttack::Pinch;
        CHECK(frettingStopAt(pinch, 5) == frettedStop(5));
        CHECK(soundingStopAt(pinch.harmonic_node, pinch.attack, pinch.fret, 5) == frettedStop(5));
        // An artificial harmonic presses a real stop while the picking hand damps the node —
        // asserted BESIDE the sounding answer, so the two producers are never collapsed into one.
        ChartNote artificial = natural(17.0);
        artificial.fret = 5;
        CHECK(frettingStopAt(artificial, 5) == frettedStop(5));
        CHECK(
            soundingStopAt(artificial.harmonic_node, artificial.attack, artificial.fret, 5) ==
            nodeStop(17.0));
        // A plain note is its fret, at whatever point of its travel is asked.
        ChartNote plain = natural(0.0);
        plain.harmonic_node.reset();
        plain.fret = 7;
        CHECK(frettingStopAt(plain, 9) == frettedStop(9));
    }

    SECTION("postures compare and key by stop, so a node grip and a fret grip are two rows")
    {
        // The odr-use fixture: the defaulted comparisons on ChartStop and ChartPosture, and the
        // ordering a posture vector keys the derivation's dedup map by, all instantiated here.
        const ChartPosture node_grip{.stops = {nodeStop(12.0), std::nullopt, nodeStop(12.0)}};
        const ChartPosture same_grip{.stops = {nodeStop(12.0), std::nullopt, nodeStop(12.0)}};
        const ChartPosture fret_grip{.stops = {frettedStop(12), std::nullopt, frettedStop(12)}};
        CHECK(node_grip == same_grip);
        CHECK(node_grip != fret_grip);

        std::map<std::vector<std::optional<ChartStop>>, int> keyed;
        keyed.try_emplace(node_grip.stops, 1);
        keyed.try_emplace(fret_grip.stops, 2);
        keyed.try_emplace(same_grip.stops, 3);
        CHECK(keyed.size() == 2);
        CHECK(keyed.at(node_grip.stops) == 1);
        CHECK(keyed.at(fret_grip.stops) == 2);
    }

    SECTION("a NaN node is refused, so the posture key's ordering is always strict-weak")
    {
        const TempoMap tempo_map = makeTempoMap();
        ChartTuning tuning;
        tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = 0;
        note.sustain = Fraction{1};
        note.harmonic_node = 12.0;
        REQUIRE(validateChartNoteAlone(note, tuning, tempo_map).has_value());
        // A negative-form range test has both halves false for NaN, so NaN would pass it; the
        // positive form states what a legal node IS.
        note.harmonic_node = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(validateChartNoteAlone(note, tuning, tempo_map).has_value());
    }
}

// The tick lattice is the finest position a chart may state, and every stored instant is held to
// it: the onset, where the ring ends, and each keyframe. A 4/4 beat holds 960 ticks, so a whole
// number of 960ths lands on the lattice and half a tick past one does not.
TEST_CASE("Chart rules refuse an instant between two ticks", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 960}};
    note.string = 1;
    note.fret = 5;
    note.sustain = Fraction{1};
    note.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 7}};
    REQUIRE(validateChartNoteAlone(note, tuning, tempo_map).has_value());

    SECTION("an onset between two ticks")
    {
        note.position.offset = Fraction{1, 1920};
        CHECK_FALSE(validateChartNoteAlone(note, tuning, tempo_map).has_value());
    }
    SECTION("a ring ending between two ticks")
    {
        note.sustain = Fraction{1920 + 1, 1920};
        CHECK_FALSE(validateChartNoteAlone(note, tuning, tempo_map).has_value());
    }
    SECTION("a keyframe between two ticks, however close to one")
    {
        note.keyframes.front().offset = Fraction{99, 1600};
        CHECK_FALSE(validateChartNoteAlone(note, tuning, tempo_map).has_value());
    }
    SECTION("a septuplet instant, which no tick divides")
    {
        note.position.offset = Fraction{4, 7};
        CHECK_FALSE(validateChartNoteAlone(note, tuning, tempo_map).has_value());
    }
}

// The harmonic rules: which node-and-attack records are legal, and where a node may lie.
TEST_CASE("Chart harmonics are a node plus an attack", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    const auto note_at = [](const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = fret;
        note.sustain = g_fixture_ring;
        return note;
    };
    const auto round_trip = [&tempo_map](const ChartNote& note) {
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note};
        const auto parsed = parseChartDocument(chartDocumentText(chart, tempo_map));
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
        // comparison is strict — a node AT the stop is the stop. Probed through a pinch, the one
        // harmonic still allowed over a pressed stop while the artificial form is disabled.
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note_at(3)};
        chart.notes[0].attack = NoteAttack::Pinch;
        chart.notes[0].harmonic_node = 3.0;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = 2.7;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        chart.notes[0].harmonic_node = 3.2;
        CHECK(validateChartRules(chart, tempo_map).has_value());
    }

    SECTION("artificial and tapped harmonics are refused for now")
    {
        // Only the natural and the pinch are supported: a node over a pressed stop under a
        // fretting-hand attack, and a node under the tap attack, are refused by name so no chart
        // can hold either until the forms are deliberately reopened. The reading code that would
        // draw them stays behind this one rule.
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note_at(5)};
        chart.notes[0].harmonic_node = 17.0;
        const auto artificial = validateChartRules(chart, tempo_map);
        REQUIRE_FALSE(artificial.has_value());
        CHECK(artificial.error().code == ChartErrorCode::InvalidNote);
        CHECK(artificial.error().message.find("not supported yet") != std::string::npos);

        chart.notes[0].attack = NoteAttack::Tap;
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        // The tap is refused even over the open string, where it presses nothing: the attack is
        // the disabled form, not the stop.
        chart.notes[0].fret = 0;
        chart.notes[0].harmonic_node = 12.0;
        const auto tapped = validateChartRules(chart, tempo_map);
        REQUIRE_FALSE(tapped.has_value());
        CHECK(tapped.error().message.find("not supported yet") != std::string::npos);

        // The controls: the same records without the disabled element are legal.
        chart.notes[0].attack = NoteAttack::Pick;
        CHECK(validateChartRules(chart, tempo_map).has_value());
        chart.notes[0].fret = 5;
        chart.notes[0].harmonic_node = 17.0;
        chart.notes[0].attack = NoteAttack::Pinch;
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
        // g_max_fret. A pinch's thumb grazes over the body, so it escapes the bound (only the
        // universal 48 limit applies to it). The tapped node would escape it the same way, but the
        // tapped form is refused outright while it is disabled, so it cannot be probed here.
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
        // Loading an out-of-date package must fail loudly: ignoring these keys would drop every
        // harmonic in the chart without a word.
        CHECK_FALSE(
            parseChartDocument(
                R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                R"( "notes": [ { "position": "1:1", "string": 1, "fret": 0, "sustain": "1/8",)"
                R"( "harmonic": "natural" } ] })")
                .has_value());
        CHECK_FALSE(
            parseChartDocument(
                R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                R"( "notes": [ { "position": "1:1", "string": 1, "fret": 12, "sustain": "1/8",)"
                R"( "touch": 12.0 } ] })")
                .has_value());
    }
}

// The claim both drawing surfaces read for their harmonic mark — the 2D diamond head and the 3D
// harmonic cell — kept apart from soundingStopAt's claim about WHERE the note sounds, because the
// two part company at the pinch.
TEST_CASE("isHarmonic names a harmonic whichever hand makes it", "[core][chart]")
{
    // The thumb's squeal is stated by the ATTACK, so a pinch counts whether or not the charter has
    // picked out which overtone it grazes. A saved pinch always carries a node, but the editor
    // holds one that does not while the picker is still open.
    CHECK(isHarmonic(24.0, NoteAttack::Pinch));
    CHECK(isHarmonic(std::nullopt, NoteAttack::Pinch));

    // Every other harmonic counts by carrying a node, whichever hand touches it: the fretting
    // hand's finger on a natural harmonic's node, or the picking hand's on a tapped one.
    CHECK(isHarmonic(12.0, NoteAttack::Pick));
    CHECK(isHarmonic(17.0, NoteAttack::Tap));

    // A scrape's node is the in-memory latent its attack toggle preserves rather than a touch
    // anybody makes, so it is no harmonic and keeps the plectrum silhouette.
    CHECK_FALSE(isHarmonic(12.0, NoteAttack::PickSlide));

    // And a note with nothing touching it is a plain note.
    CHECK_FALSE(isHarmonic(std::nullopt, NoteAttack::Pick));
}

// The one name for the family whose head prints somewhere the fretting hand is not, which is why
// both surfaces state that hand's stop beside the head — the 2D lane in the satellite, the highway
// as the floor line's run from the stop to the node.
TEST_CASE("harmonicOverPressedStop names the harmonics whose stop the head omits", "[core][chart]")
{
    // An ARTIFICIAL harmonic: the fretting hand presses 5 while the picking hand touches the node
    // above it, so the 17 the head prints says nothing about where that hand is.
    CHECK(harmonicOverPressedStop(5, 17.0, NoteAttack::Pick));
    // A TAPPED harmonic stores those same two numbers and differs only in how the picking hand
    // sounds the string, which is exactly why the two are one case here.
    CHECK(harmonicOverPressedStop(5, 17.0, NoteAttack::Tap));

    // A NATURAL harmonic's finger is on the node the head prints, so nothing is displaced and the
    // note has one number to state.
    CHECK_FALSE(harmonicOverPressedStop(0, 12.0, NoteAttack::Pick));
    // A PINCH's node lies over the body rather than the neck, so its head prints the very fret the
    // fretting hand presses.
    CHECK_FALSE(harmonicOverPressedStop(5, 29.0, NoteAttack::Pinch));
    // A PLAIN note has nothing touching it at all.
    CHECK_FALSE(harmonicOverPressedStop(5, std::nullopt, NoteAttack::Pick));
    // A SCRAPE's node is the in-memory latent its attack toggle preserves rather than a touch
    // anybody makes, so there is no second place for the head to print.
    CHECK_FALSE(harmonicOverPressedStop(5, 12.0, NoteAttack::PickSlide));

    // The note overload reads those same three fields, so no caller can ask a different question by
    // handing over the note itself.
    ChartNote artificial;
    artificial.position = GridPosition{.measure = 1, .beat = 1};
    artificial.string = 1;
    artificial.fret = 5;
    artificial.harmonic_node = 17.0;
    CHECK(harmonicOverPressedStop(artificial));
    artificial.fret = 0;
    CHECK_FALSE(harmonicOverPressedStop(artificial));
}

// The chart-level half of the removed-field tripwire: the posture table and its spans are derived
// from the notes (deriveChartShapes), so a document carrying either key states a second,
// unverifiable copy of what the notes already say. Silently ignoring them is the failure this
// pins — an out-of-date package would load with its stored picture discarded and no word said.
// Delete this with the tripwire.
TEST_CASE("Chart document refuses the removed posture and span keys", "[core][chart]")
{
    const auto parse_with_key = [](const std::string& key_body) {
        return parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] }, )" + key_body +
            R"( "notes": [] })");
    };
    const auto chords = parse_with_key(R"("chords": [],)");
    REQUIRE_FALSE(chords.has_value());
    CHECK(chords.error().message.find("re-import") != std::string::npos);
    CHECK_FALSE(parse_with_key(R"("shapes": [],)").has_value());
    // Populated, not just present: the refusal is about the key existing at all, so the shape of
    // its contents cannot make it load.
    CHECK_FALSE(parse_with_key(R"("chords": [ { "frets": [0] } ],)").has_value());
    // The control: the same document without them loads.
    CHECK(parse_with_key("").has_value());
}

// The keyframe array is the format's one interval payload, and every channel it carries is
// PRESENCE-keyed: an unstated channel is a meaning (the reading passes through it), not a
// defaulted value. This pins the writer against eliding a stated channel that happens to look
// like a default — a bend released back to zero states a value a shorter spelling would silently
// delete — and the vibrato width as the one per-leg channel, `None` omitted.
TEST_CASE("Chart keyframes round-trip every channel, absence included", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = 1};
    note.string = 1;
    note.fret = 5;
    note.sustain = Fraction{2};
    note.vibrato = VibratoState::Narrow;
    note.bend = 1.0;
    note.keyframes = {
        Keyframe{.offset = Fraction{1, 4}, .fret = 7},
        Keyframe{.offset = Fraction{1, 2}, .bend = 0.0},
        Keyframe{.offset = Fraction{1}, .vibrato = VibratoState::Wide},
        Keyframe{.offset = Fraction{3, 2}, .fret = 9, .bend = 2.0, .vibrato = VibratoState::Narrow},
    };

    Chart chart;
    chart.tuning.strings = {"E2"};
    chart.notes = {note};
    const std::string text = chartDocumentText(chart, tempo_map);
    const auto parsed = parseChartDocument(text);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->notes.size() == 1);
    CHECK(parsed->notes[0] == note);

    // The default-looking statement survives as a STATEMENT. Round-trip equality alone would still
    // pass if the writer dropped it and the reader defaulted it back, so it is also asserted as
    // present and its unstated neighbours as absent.
    const std::vector<Keyframe>& keyframes = parsed->notes[0].keyframes;
    REQUIRE(keyframes.size() == 4);
    CHECK(keyframes[0].fret.has_value());
    CHECK_FALSE(keyframes[0].bend.has_value());
    CHECK_FALSE(hasVibrato(keyframes[0].vibrato));
    // Each optional is bound once and guarded by that name: the checker cannot tie two separate
    // reads of an indexed element together.
    const std::optional<double>& released_bend = keyframes[1].bend;
    REQUIRE(released_bend.has_value());
    if (released_bend.has_value())
    {
        CHECK(std::is_eq(*released_bend <=> 0.0));
    }
    CHECK_FALSE(keyframes[1].fret.has_value());
    CHECK(keyframes[2].vibrato == VibratoState::Wide);
    CHECK_FALSE(keyframes[2].fret.has_value());
    // The document text itself, because that is where an elision would happen; `None` has no word.
    CHECK(text.find(R"("bend": 0)") != std::string::npos);
    CHECK(text.find(R"("vibrato": "wide")") != std::string::npos);
    CHECK(text.find(R"("vibrato": "off")") == std::string::npos);
    CHECK(text.find(R"("keyframes")") != std::string::npos);

    // The onset facts, and the elision that IS correct: a note at rest writes no bend at all,
    // because zero is the unbent onset and absence says exactly that.
    ChartNote unbent = note;
    unbent.bend = 0.0;
    unbent.keyframes.clear();
    Chart plain = chart;
    plain.notes = {unbent};
    CHECK(chartDocumentText(plain, tempo_map).find(R"("bend")") == std::string::npos);
}

// The removed payload spellings. `bend` EXISTS under a different shape, so its refusal is keyed on
// the removed shape rather than on the key: a document carrying it must name the re-import remedy
// instead of loading with its curve silently dropped. The others are removed outright — `slides`
// and `waypoints` are old spellings of the one interval-payload array, and the slide-out is the
// keyframe at the ring's end, so BOTH `slideOut` spellings are refused: the object form carrying an
// offset and the bare-fret form.
TEST_CASE("Chart document refuses the removed payload spellings", "[core][chart]")
{
    const auto parse_note = [](const std::string& body) {
        return parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
            R"( "notes": [ { "position": "1:1", "string": 1, "fret": 5, "sustain": "1/2", )" +
            body + R"( } ] })");
    };

    const auto slides = parse_note(R"("slides": [ { "offset": "1/4", "fret": 7 } ])");
    REQUIRE_FALSE(slides.has_value());
    CHECK(slides.error().message.find("re-import") != std::string::npos);

    const auto waypoints = parse_note(R"("waypoints": [ { "offset": "1/4", "fret": 7 } ])");
    REQUIRE_FALSE(waypoints.has_value());
    CHECK(waypoints.error().message.find("re-import") != std::string::npos);

    const auto bend_curve = parse_note(R"("bend": [["0", 1.0], ["1/2", 2.0]])");
    REQUIRE_FALSE(bend_curve.has_value());
    CHECK(bend_curve.error().message.find("re-import") != std::string::npos);

    const auto slide_out_object = parse_note(R"("slideOut": { "offset": "1/2", "fret": 9 })");
    REQUIRE_FALSE(slide_out_object.has_value());
    CHECK(slide_out_object.error().message.find("re-import") != std::string::npos);

    // The bare-fret form is refused too: the gesture is a keyframe, so nothing on the note states
    // it.
    const auto slide_out_fret = parse_note(R"("slideOut": 9)");
    REQUIRE_FALSE(slide_out_fret.has_value());
    CHECK(slide_out_fret.error().message.find("re-import") != std::string::npos);

    // The controls: each key in its CURRENT shape loads, so the refusals above are about the old
    // shape and not about the key existing. The slide-out's current shape is a keyframe stated at
    // the sustain, which the first control already is.
    CHECK(parse_note(R"("keyframes": [ { "offset": "1/4", "fret": 7 } ])").has_value());
    CHECK(parse_note(R"("bend": 1.0)").has_value());
    CHECK(parse_note(R"("keyframes": [ { "offset": "1/2", "fret": 9 } ])").has_value());
}

// The vibrato channel is a WIDTH axis, and the document says so in words: the ordinary vibrato is
// `"narrow"` — a description of what an ordinary vibrato physically is, a fraction of a semitone —
// and the deliberate exaggeration is `"wide"`. Absence is the only spelling of no vibrato, at an
// onset and a keyframe alike: `None` has no word.
TEST_CASE("Chart document reads the vibrato width axis", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const auto parse_note = [](const std::string& body) {
        return parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
            R"( "notes": [ { "position": "1:1", "string": 1, "fret": 5, "sustain": "1", )" +
            body + R"( } ] })");
    };

    SECTION("both widths round-trip through the writer and back")
    {
        for (const VibratoState width : {VibratoState::Narrow, VibratoState::Wide})
        {
            CAPTURE(static_cast<int>(width));
            Chart chart;
            chart.tuning.strings = {"E2"};
            ChartNote note;
            note.position = GridPosition{.measure = 1, .beat = 1};
            note.string = 1;
            note.fret = 5;
            note.sustain = Fraction{2};
            note.vibrato = width;
            // The keyframe's leg steps to the OTHER width, so across the loop each word is also
            // spelled on a keyframe: a repeated width would be silent, and the writer drops it.
            const VibratoState other =
                width == VibratoState::Narrow ? VibratoState::Wide : VibratoState::Narrow;
            note.keyframes = {
                Keyframe{.offset = Fraction{1}, .vibrato = other},
            };
            chart.notes = {note};

            const std::string text = chartDocumentText(chart, tempo_map);
            const auto reparsed = parseChartDocument(text);
            REQUIRE(reparsed.has_value());
            if (!reparsed.has_value())
            {
                return;
            }
            REQUIRE(reparsed->notes.size() == 1);
            CHECK(reparsed->notes[0].vibrato == width);
            REQUIRE(reparsed->notes[0].keyframes.size() == 1);
            CHECK(reparsed->notes[0].keyframes[0].vibrato == other);
        }
    }

    SECTION("a note that does not vibrate writes no key at all")
    {
        Chart chart;
        chart.tuning.strings = {"E2"};
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = 5;
        note.sustain = Fraction{1};
        chart.notes = {note};
        CHECK(chartDocumentText(chart, tempo_map).find(R"("vibrato")") == std::string::npos);
    }

    SECTION("an onset may not say off, because absence already says it")
    {
        // The no-`"pick"`-token shape: the writer can never produce this word here, so a document
        // carrying it was written by something that does not know the format.
        const auto off = parse_note(R"("vibrato": "off")");
        REQUIRE_FALSE(off.has_value());
        CHECK(off.error().message.find("vibrato is unknown") != std::string::npos);
        CHECK_FALSE(parse_note(R"("vibrato": "slight")").has_value());
        // The controls: both real widths load at the onset.
        CHECK(parse_note(R"("vibrato": "narrow")").has_value());
        CHECK(parse_note(R"("vibrato": "wide")").has_value());
    }

    SECTION("a keyframe reads the same two words, and off is unknown there too")
    {
        // Nothing carries, so a keyframe never has to say vibrato ends, and `off` is a read error
        // there exactly as at the onset.
        const auto off = parse_note(R"("keyframes": [ { "offset": "1/2", "vibrato": "off" } ])");
        REQUIRE_FALSE(off.has_value());
        CHECK(off.error().message.find("vibrato is unknown") != std::string::npos);
        CHECK_FALSE(
            parse_note(R"("keyframes": [ { "offset": "1/2", "vibrato": "none" } ])").has_value());
        CHECK(
            parse_note(R"("keyframes": [ { "offset": "1/2", "vibrato": "narrow" } ])").has_value());
        CHECK(parse_note(R"("keyframes": [ { "offset": "1/2", "vibrato": "wide" } ])").has_value());
        const auto unknown =
            parse_note(R"("keyframes": [ { "offset": "1/2", "vibrato": "slight" } ])");
        REQUIRE_FALSE(unknown.has_value());
        CHECK(unknown.error().message.find("vibrato is unknown") != std::string::npos);
    }

    SECTION("the boolean spelling is refused with the re-import remedy, at both scopes")
    {
        // A bare "wrong type" message would describe the symptom without naming the fix, which is
        // what the removed-spelling rows exist to avoid.
        const auto onset = parse_note(R"("vibrato": true)");
        REQUIRE_FALSE(onset.has_value());
        CHECK(
            onset.error().message.find(R"(re-import the package to get "vibrato": "narrow")") !=
            std::string::npos);

        const auto keyframe =
            parse_note(R"("keyframes": [ { "offset": "1/2", "vibrato": false } ])");
        REQUIRE_FALSE(keyframe.has_value());
        CHECK(
            keyframe.error().message.find(R"(re-import the package to get "vibrato": "narrow")") !=
            std::string::npos);
    }
}

// The one classifier for the axis, which every consumer asks instead of comparing against a
// width: an open-coded `== Narrow` would answer "no vibrato" for the wide notes it was never
// told about, exactly the trap isAccented exists to close on the emphasis axis.
TEST_CASE("Chart vibrato classifies every width as vibrating", "[core][chart]")
{
    CHECK_FALSE(hasVibrato(VibratoState::None));
    CHECK(hasVibrato(VibratoState::Narrow));
    CHECK(hasVibrato(VibratoState::Wide));
    // Value-initialization lands on no vibrato, which is why None is declared first: a
    // default-constructed or resized note must not arrive already vibrating.
    CHECK_FALSE(hasVibrato(VibratoState{}));
    CHECK_FALSE(hasVibrato(ChartNote{}.vibrato));
}

// The leg an instant lies in, read as a keyframe inserted there would divide it: a keyframe
// standing AT the instant begins the leg after it, so the answer is the leg before that one.
TEST_CASE("vibratoBefore reads the leg an instant lies in", "[core][chart]")
{
    const ChartNote note = vibratoLegNote(
        VibratoState::Narrow,
        {Keyframe{.offset = Fraction{1}, .fret = 7, .vibrato = VibratoState::Wide},
         Keyframe{.offset = Fraction{2}, .fret = 7}});

    // The onset's leg.
    CHECK(vibratoBefore(note, Fraction{1, 2}) == VibratoState::Narrow);
    // A keyframe's leg, and the unvibrated leg the last one begins.
    CHECK(vibratoBefore(note, Fraction{3, 2}) == VibratoState::Wide);
    CHECK(vibratoBefore(note, Fraction{3}) == VibratoState::None);
    // A keyframe AT the instant is ignored: the answer is the leg it ends, the onset's at the first
    // and the first keyframe's at the second.
    CHECK(vibratoBefore(note, Fraction{1}) == VibratoState::Narrow);
    CHECK(vibratoBefore(note, Fraction{2}) == VibratoState::Wide);
}

// Every creator of a mid-ring keyframe starts from this record, so a point planted for its fret or
// its bend continues the leg it divides instead of beginning an unvibrated one.
TEST_CASE("keyframeInLeg carries the width of the leg it divides", "[core][chart]")
{
    const ChartNote note =
        vibratoLegNote(VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .fret = 5}});

    SECTION("inside a vibrated leg it carries that leg's width and states nothing else")
    {
        const Keyframe point = keyframeInLeg(note, Fraction{1});
        CHECK(point.offset == Fraction{1});
        CHECK_FALSE(point.fret.has_value());
        CHECK_FALSE(point.bend.has_value());
        CHECK(point.vibrato == VibratoState::Narrow);

        // Stating a fret on top plants a legal point that leaves the vibrato running through it.
        ChartNote planted = note;
        Keyframe stated = point;
        stated.fret = 9;
        planted.keyframes.insert(planted.keyframes.begin(), stated);
        CHECK(acceptedAlone(planted));
        CHECK(ringStateAt(planted, Fraction{3, 2}).vibrato == VibratoState::Narrow);
    }

    SECTION("inside an unvibrated leg it carries None")
    {
        const Keyframe point = keyframeInLeg(note, Fraction{3});
        CHECK(point.offset == Fraction{3});
        CHECK_FALSE(point.fret.has_value());
        CHECK_FALSE(point.bend.has_value());
        CHECK(point.vibrato == VibratoState::None);
    }
}

// Every statement at one instant shares ONE keyframe: keyframeAt reaches the one standing there,
// and plants one carrying the leg's width, in order, only where none stands.
TEST_CASE(
    "keyframeAt reaches the keyframe at an instant, planting one only where none stands",
    "[core][chart]")
{
    ChartNote note =
        vibratoLegNote(VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .fret = 5}});

    SECTION("a keyframe standing there is the one reached")
    {
        keyframeAt(note, Fraction{2}).bend = 1.0;
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].fret == 5);
        CHECK(note.keyframes[0].bend.has_value());
    }
    SECTION("where none stands one is planted in order, carrying the leg's width")
    {
        keyframeAt(note, Fraction{1}).bend = 1.0;
        REQUIRE(note.keyframes.size() == 2);
        CHECK(note.keyframes[0].offset == Fraction{1});
        CHECK_FALSE(note.keyframes[0].fret.has_value());
        CHECK(note.keyframes[0].vibrato == VibratoState::Narrow);
        CHECK(note.keyframes[1].offset == Fraction{2});
    }
}

// The one stored form of a vibrato ending: the keyframe at the instant takes width None, bare
// where it states nothing else, and none is created where the leg before it did not vibrate.
// Nothing is ever erased here — a silent point is the commit law's to sweep.
TEST_CASE("endVibratoAt stores a vibrato ending in its one form", "[core][chart]")
{
    SECTION("no keyframe after a vibrated leg: a bare keyframe is created")
    {
        ChartNote note = vibratoLegNote(VibratoState::Narrow, {});
        endVibratoAt(note, Fraction{2});
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{2});
        CHECK_FALSE(note.keyframes[0].fret.has_value());
        CHECK_FALSE(note.keyframes[0].bend.has_value());
        CHECK(note.keyframes[0].vibrato == VibratoState::None);
        CHECK(acceptedAlone(note));
    }

    SECTION("the path is untouched: a bare ending inside a glide leaves the glide whole")
    {
        // A glide from 5 to 7 over two beats. A fret restated at one beat would have split it into
        // a hold and a glide; the bare ending states no position, so the path is the same stops.
        const ChartNote gliding =
            vibratoLegNote(VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .fret = 7}});
        ChartNote ending = gliding;
        endVibratoAt(ending, Fraction{1});
        REQUIRE(ending.keyframes.size() == 2);
        CHECK(ending.keyframes[0].offset == Fraction{1});
        CHECK_FALSE(ending.keyframes[0].fret.has_value());
        CHECK(ending.keyframes[0].vibrato == VibratoState::None);
        // Writing it is this function's whole job; the chart then refuses it, since no vibrato
        // change may stand where the hand travels (shedMidTravelVibrato).
        CHECK_FALSE(acceptedAlone(ending));

        const TempoMap tempo_map = makeTempoMap();
        const auto project = [&tempo_map](const ChartNote& note) {
            Chart chart;
            chart.tuning.strings = {"E2"};
            chart.notes = {note};
            Arrangement arrangement;
            arrangement.chart = std::move(chart);
            return makeChartViewState(arrangement, tempo_map);
        };
        const ChartViewState with = project(ending);
        const ChartViewState without = project(gliding);
        REQUIRE(with.notes.size() == 1);
        REQUIRE(without.notes.size() == 1);
        const std::vector<SlideStopViewState>& stops = with.notes.front().slides;
        const std::vector<SlideStopViewState>& reference = without.notes.front().slides;
        REQUIRE(stops.size() == 1);
        REQUIRE(reference.size() == 1);
        CHECK(stops[0].fret == 7);
        CHECK(stops[0].fret == reference[0].fret);
        CHECK_THAT(stops[0].seconds, Catch::Matchers::WithinULP(reference[0].seconds, 0));
        // What the ending DOES change is the vibrato: one region, closed at the bare keyframe.
        REQUIRE(with.notes.front().vibrato.size() == 1);
        CHECK(with.notes.front().vibrato[0].end_seconds == Catch::Approx(0.5));
    }

    SECTION("no keyframe after an unvibrated leg: nothing is created")
    {
        // The leg before the instant is the one the stop at 1 begins, not the vibrated onset's.
        ChartNote note =
            vibratoLegNote(VibratoState::Narrow, {Keyframe{.offset = Fraction{1}, .fret = 5}});
        const ChartNote before = note;
        endVibratoAt(note, Fraction{2});
        CHECK(note == before);
        CHECK(acceptedAlone(note));

        ChartNote plain = vibratoLegNote(VibratoState::None, {});
        endVibratoAt(plain, Fraction{2});
        CHECK(plain.keyframes.empty());
        CHECK(acceptedAlone(plain));
    }

    SECTION("a width-only keyframe after a vibrated leg becomes bare")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Wide}});
        endVibratoAt(note, Fraction{2});
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{2});
        CHECK_FALSE(note.keyframes[0].fret.has_value());
        CHECK_FALSE(note.keyframes[0].bend.has_value());
        CHECK(note.keyframes[0].vibrato == VibratoState::None);
        CHECK(acceptedAlone(note));
    }

    SECTION("a width-only keyframe after an unvibrated leg stays bare, and is silent")
    {
        // endVibratoAt never erases: the point stays as the bare beginning of a leg, and whether
        // it says anything is the commit law's question, which calls it silent here.
        ChartNote note = vibratoLegNote(
            VibratoState::None, {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow}});
        endVibratoAt(note, Fraction{2});
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{2});
        CHECK(keyframeStatesNothing(note.keyframes[0]));
        CHECK(keyframeSaysNothingNew(vibratoLegNote(VibratoState::None, {}), note.keyframes[0]));
        CHECK(acceptedAlone(note));
        CHECK(stripSilentKeyframes(note));
        CHECK(note.keyframes.empty());
    }

    SECTION("a keyframe stating a fret loses its width and keeps its fret")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::Narrow,
            {Keyframe{.offset = Fraction{2}, .fret = 7, .vibrato = VibratoState::Wide}});
        endVibratoAt(note, Fraction{2});
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{2});
        CHECK(note.keyframes[0].fret == 7);
        CHECK_FALSE(note.keyframes[0].bend.has_value());
        CHECK(note.keyframes[0].vibrato == VibratoState::None);
        CHECK(acceptedAlone(note));
    }
}

// Structural and nothing more: whether a keyframe carries a channel at all, asked of the keyframe
// alone. Whether a bare one SAYS anything is the commit law's question, tested below.
TEST_CASE("keyframeStatesNothing reports a keyframe carrying no channel", "[core][chart]")
{
    CHECK(keyframeStatesNothing(Keyframe{.offset = Fraction{2}}));
    CHECK_FALSE(keyframeStatesNothing(Keyframe{.offset = Fraction{2}, .fret = 7}));
    CHECK_FALSE(keyframeStatesNothing(Keyframe{.offset = Fraction{2}, .bend = 1.0}));
    CHECK_FALSE(
        keyframeStatesNothing(Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow}));
}

// The strip clears channels, and a keyframe it empties INSIDE the ring stays as a bare leg
// boundary, left to the commit law to judge like any other point. One it empties AT the ring's
// end goes: no leg begins there, so a bare keyframe at the end is nothing.
TEST_CASE("stripKeyframeChannels clears channels and keeps an emptied keyframe", "[core][chart]")
{
    const auto strip_fret = [](Keyframe& keyframe) {
        const bool had_fret = keyframe.fret.has_value();
        keyframe.fret.reset();
        return had_fret;
    };

    SECTION("an emptied keyframe inside the ring is kept")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::None,
            {
                Keyframe{.offset = Fraction{1}, .fret = 7},
                Keyframe{.offset = Fraction{2}, .fret = 9, .bend = 1.0},
                Keyframe{.offset = Fraction{3}, .bend = 0.5},
            });
        CHECK(stripKeyframeChannels(note, strip_fret));
        REQUIRE(note.keyframes.size() == 3);
        CHECK(note.keyframes[0].offset == Fraction{1});
        CHECK(keyframeStatesNothing(note.keyframes[0]));
        CHECK_FALSE(note.keyframes[1].fret.has_value());
        CHECK(note.keyframes[1].bend.has_value());
        CHECK(note.keyframes[2].bend.has_value());

        // A strip that clears nothing reports nothing.
        CHECK_FALSE(stripKeyframeChannels(note, [](Keyframe&) { return false; }));
        CHECK(note.keyframes.size() == 3);
    }

    SECTION("an emptied keyframe at the ring's end is dropped")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::None,
            {
                Keyframe{.offset = Fraction{2}, .fret = 7},
                Keyframe{.offset = Fraction{4}, .fret = 9},
            });
        CHECK(stripKeyframeChannels(note, strip_fret));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{2});
        CHECK(keyframeStatesNothing(note.keyframes[0]));
    }

    SECTION("a keyframe at the ring's end that keeps a channel stays")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::None, {Keyframe{.offset = Fraction{4}, .fret = 9, .bend = 1.0}});
        CHECK(stripKeyframeChannels(note, strip_fret));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{4});
        CHECK(note.keyframes[0].bend.has_value());
    }
}

// The vibrato channel's statement is the LEG a keyframe begins, so whether a keyframe carrying no
// fret and no bend says anything is a question about the leg before it — and at the ring's end,
// where no leg begins, it never does. The law is asked of the note WITHOUT the point.
TEST_CASE("keyframeSaysNothingNew judges a width against the leg before it", "[core][chart]")
{
    const Keyframe bare{.offset = Fraction{2}};

    SECTION("a bare keyframe after a vibrated leg ends the vibrato and says so")
    {
        CHECK_FALSE(keyframeSaysNothingNew(vibratoLegNote(VibratoState::Narrow, {}), bare));
        CHECK(acceptedAlone(vibratoLegNote(VibratoState::Narrow, {bare})));
    }

    SECTION("a bare keyframe after an unvibrated leg says nothing, and is still legal")
    {
        CHECK(keyframeSaysNothingNew(vibratoLegNote(VibratoState::None, {}), bare));
        CHECK(acceptedAlone(vibratoLegNote(VibratoState::None, {bare})));

        // The leg before is the one the stop at 1 begins, not the vibrated onset's.
        const ChartNote stopped =
            vibratoLegNote(VibratoState::Narrow, {Keyframe{.offset = Fraction{1}, .fret = 7}});
        CHECK(keyframeSaysNothingNew(stopped, bare));
    }

    SECTION("a width equal to the leg before it says nothing, a different one begins a leg")
    {
        const ChartNote narrow = vibratoLegNote(VibratoState::Narrow, {});
        CHECK(keyframeSaysNothingNew(
            narrow, Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow}));
        CHECK_FALSE(keyframeSaysNothingNew(
            narrow, Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Wide}));
    }

    SECTION("at the ring's end no leg begins, so no width says anything there")
    {
        const Keyframe end_bare{.offset = Fraction{4}};
        for (const VibratoState onset : {VibratoState::None, VibratoState::Narrow})
        {
            CHECK(keyframeSaysNothingNew(vibratoLegNote(onset, {}), end_bare));
        }
        CHECK(keyframeSaysNothingNew(
            vibratoLegNote(VibratoState::None, {}),
            Keyframe{.offset = Fraction{4}, .vibrato = VibratoState::Wide}));
        // A fret there is the slide-out, a statement whatever the leg before.
        CHECK_FALSE(keyframeSaysNothingNew(
            vibratoLegNote(VibratoState::Narrow, {}), Keyframe{.offset = Fraction{4}, .fret = 7}));
    }
}

// The commit law is asked of each statement too: a point standing for one channel does not keep a
// silent statement beside it, so a fret retyped to the one in force on a vibrato change goes, and
// the point stays for its vibrato.
TEST_CASE(
    "stripSilentKeyframes withdraws a silent statement from a point that stands", "[core][chart]")
{
    SECTION("the fret in force beside a vibrato change")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::None,
            {Keyframe{.offset = Fraction{2}, .fret = 5, .vibrato = VibratoState::Narrow}});
        CHECK(
            shedSilentStatements(vibratoLegNote(VibratoState::None, {}), note.keyframes[0]) ==
            Keyframe{.offset = Fraction{2}, .fret = {}, .vibrato = VibratoState::Narrow});
        CHECK(stripSilentKeyframes(note));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(
            note.keyframes[0] ==
            Keyframe{.offset = Fraction{2}, .fret = {}, .vibrato = VibratoState::Narrow});
    }

    SECTION("a bend at rest where the curve rests beside a new fret")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::None, {Keyframe{.offset = Fraction{2}, .fret = 7, .bend = 0.0}});
        CHECK(stripSilentKeyframes(note));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0] == Keyframe{.offset = Fraction{2}, .fret = 7, .bend = {}});
    }

    SECTION("a point whose every statement says something is untouched")
    {
        ChartNote note = vibratoLegNote(
            VibratoState::None,
            {Keyframe{
                .offset = Fraction{2}, .fret = 7, .bend = 1.0, .vibrato = VibratoState::Narrow
            }});
        CHECK_FALSE(stripSilentKeyframes(note));
        CHECK(note.keyframes.size() == 1);
    }
}

// A keyframe is a leg boundary and may carry any subset of its channels, none included: a bare
// one is legal, and where it says nothing the commit law sweeps it. The channels it may state are
// bounded — a fret is a real position, and a bend is a PUSH, which a finger cannot make downward
// (W9-K).
TEST_CASE("Chart rules bound a keyframe's channels", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const auto validate_with = [&tempo_map](const Keyframe& keyframe, const double onset_bend) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = 5;
        note.sustain = Fraction{1};
        note.bend = onset_bend;
        note.keyframes = {keyframe};
        Chart chart;
        chart.tuning.strings = {"E2"};
        chart.notes = {note};
        return validateChartRules(chart, tempo_map);
    };
    const auto refuses = [&validate_with](const Keyframe& keyframe, const double onset_bend = 0.0) {
        const auto result = validate_with(keyframe, onset_bend);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ChartErrorCode::InvalidNotePayload);
    };

    SECTION("a keyframe carrying no channel is legal, and any one channel is too")
    {
        CHECK(validate_with(Keyframe{.offset = Fraction{1, 2}}, 0.0).has_value());
        CHECK(validate_with(Keyframe{.offset = Fraction{1, 2}, .fret = 7}, 0.0).has_value());
        CHECK(validate_with(Keyframe{.offset = Fraction{1, 2}, .bend = 1.0}, 0.0).has_value());
        CHECK(
            validate_with(Keyframe{.offset = Fraction{1, 2}, .vibrato = VibratoState::Narrow}, 0.0)
                .has_value());

        // On the LOAD path, which normalizes before it validates (rock_song_package_read.cpp),
        // a bare keyframe after an unvibrated leg says nothing, so the commit law sweeps it and
        // says so; the note that remains validates.
        Chart loaded;
        loaded.tuning.strings = {"E2"};
        ChartNote empty_statement;
        empty_statement.position = GridPosition{.measure = 1, .beat = 1};
        empty_statement.string = 1;
        empty_statement.fret = 5;
        empty_statement.sustain = Fraction{1};
        empty_statement.keyframes = {Keyframe{.offset = Fraction{1, 2}}};
        loaded.notes = {empty_statement};
        const std::vector<ChartConversion> conversions = normalizeChart(loaded, tempo_map);
        REQUIRE(conversions.size() == 1);
        CHECK(conversions.front().repair == ChartRepair::SilentKeyframe);
        REQUIRE(loaded.notes.size() == 1);
        CHECK(loaded.notes[0].keyframes.empty());
        CHECK(validateChartRules(loaded, tempo_map).has_value());
    }

    SECTION("offsets are strictly inside the ring, and offset zero is the onset's own")
    {
        // Zero would be a second spelling of a value the note itself already states.
        refuses(Keyframe{.offset = Fraction{}, .fret = 7});
        refuses(Keyframe{.offset = Fraction{3, 2}, .fret = 7});
        // The ring's own end is inclusive: a fret stated there is the SLIDE-OUT, the one statement
        // whose whole meaning is that the sound stops at it.
        CHECK(validate_with(Keyframe{.offset = Fraction{1}, .fret = 7}, 0.0).has_value());
    }

    SECTION("a bend is a push, never a pull, at the onset and along the ring alike")
    {
        refuses(Keyframe{.offset = Fraction{1, 2}, .bend = -0.5});
        refuses(Keyframe{.offset = Fraction{1, 2}, .fret = 7}, -0.5);
        // The same amounts upward are ordinary data, so the refusals are about the SIGN.
        CHECK(validate_with(Keyframe{.offset = Fraction{1, 2}, .bend = 0.5}, 0.5).has_value());
    }

    SECTION("a stated fret is a real position")
    {
        refuses(Keyframe{.offset = Fraction{1, 2}, .fret = -1});
        // Fret zero is not a negative but a stop at the nut, which is nothing pressed: the capo
        // floor STRIPS it rather than refusing the payload, so it fails through the normalizer's
        // fixpoint under a different code. Asserting that difference is what keeps the two rules
        // from being read as one.
        const auto at_the_nut = validate_with(Keyframe{.offset = Fraction{1, 2}, .fret = 0}, 0.0);
        REQUIRE_FALSE(at_the_nut.has_value());
        CHECK(at_the_nut.error().code == ChartErrorCode::InvalidNote);
        CHECK(validate_with(Keyframe{.offset = Fraction{1, 2}, .fret = 1}, 0.0).has_value());
    }
}

// A NOTE'S ARRIVAL TRUNCATES ITS PREDECESSOR'S RING, AND THE STATEMENT AT THAT RING'S END RIDES
// BACK WITH IT. The statement's moment is the end by definition, so a cut ring carries it to the
// new end whatever it states — the bend curve's last value exactly as a slide-out's fret. Losing it
// would silently turn a bend that completes as the note ends into one that completes early and then
// holds flat to an end it does not reach.
TEST_CASE("A truncation carries the statement at the ring's end", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const auto note_at = [](const GridPosition position, const Fraction sustain, const int fret) {
        ChartNote note;
        note.position = position;
        note.string = 1;
        note.fret = fret;
        note.sustain = sustain;
        return note;
    };
    // Three beats of ring on a string struck again two beats in: the truncation cuts to exact
    // adjacency with that strike.
    std::vector<ChartNote> notes{
        note_at(GridPosition{.measure = 1, .beat = 1}, Fraction{3}, 5),
        note_at(GridPosition{.measure = 1, .beat = 3}, Fraction{1}, 7),
    };

    SECTION("a bend-only end statement keeps its value at the new end")
    {
        notes[0].keyframes = {
            Keyframe{.offset = Fraction{1, 2}, .bend = 1.0},
            Keyframe{.offset = Fraction{3}, .bend = 0.0},
        };
        CHECK(
            normalizeSustainOverlaps(notes, tempo_map) ==
            std::vector<TailTruncation>{TailTruncation{.index = 0, .statement_lost = false}});
        CHECK(notes[0].sustain == Fraction{2});
        REQUIRE(notes[0].keyframes.size() == 2);
        // Bound once so every read below is provably the same object.
        const Keyframe& ends = notes[0].keyframes[1];
        CHECK(ends.offset == Fraction{2});
        const std::optional<double>& reached = ends.bend;
        REQUIRE(reached.has_value());
        if (reached.has_value())
        {
            CHECK(std::is_eq(*reached <=> 0.0));
        }
        // It states no fret, so the carry makes no slide-out of it.
        CHECK(endStatedFretOrNull(notes[0]) == nullptr);
        // The new end IS the next head of its own string, and the statement STAYS THERE: the store
        // holds what the hands did, and nothing in it spaces a mark. The truncation is its own
        // fixpoint.
        CHECK(normalizeSustainOverlaps(notes, tempo_map).empty());
        CHECK(notes[0].sustain == Fraction{2});
        CHECK(notes[0].keyframes[1].offset == Fraction{2});
    }

    SECTION("a stated fret carried onto the head is the slide-out there and stays on it")
    {
        // A fret at the ring's end that names NO stop the next head takes is the SLIDE-OUT, so a
        // ring cut back onto its own statement slides out toward it — on the head itself, which is
        // where the material says the slide-out completes. Here it names 7 and the head is struck
        // at 5, so the relation refuses the arrival reading (arrivesIntoNextHead, clause 5).
        notes[0].keyframes = {Keyframe{.offset = Fraction{2}, .fret = 7}};
        CHECK(
            normalizeSustainOverlaps(notes, tempo_map) ==
            std::vector<TailTruncation>{TailTruncation{.index = 0, .statement_lost = false}});
        CHECK(notes[0].sustain == Fraction{2});
        REQUIRE(notes[0].keyframes.size() == 1);
        CHECK(notes[0].keyframes.front().offset == Fraction{2});
        const int* const slide_out = endStatedFretOrNull(notes[0]);
        REQUIRE(slide_out != nullptr);
        if (slide_out != nullptr)
        {
            CHECK(*slide_out == 7);
        }
    }

    SECTION("a statement standing exactly at the new end takes the carried one's channels")
    {
        // The cut lands ON an interior statement, whose own moment survives it (the bound is
        // inclusive), so the end's statement arrives on top of that one instead of doubling its
        // offset.
        notes[0].keyframes = {
            Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow},
            Keyframe{.offset = Fraction{3}, .fret = 9},
        };
        CHECK(
            normalizeSustainOverlaps(notes, tempo_map) ==
            std::vector<TailTruncation>{TailTruncation{.index = 0, .statement_lost = true}});
        REQUIRE(notes[0].keyframes.size() == 1);
        const Keyframe& merged = notes[0].keyframes.front();
        CHECK(merged.offset == Fraction{2});
        // An end statement leaves no VIBRATO, whatever else it states, so the vibrato it landed on
        // goes with the ring that would have sounded it (shedEndStatementVibrato).
        CHECK(merged.fret == 9);
        CHECK_FALSE(hasVibrato(merged.vibrato));
    }
}

// THE END'S OWN STATEMENT IS SWEPT LIKE ANY OTHER POINT. Every silent point is visible authoring
// state — a slide-out toward the fret in force draws its chip exactly as an interior same-fret
// point draws its linked head — so the one focus-leave sweep clears them all, the end included, and
// the ring keeps its length. A point that says something, a slide-out that travels and a scrape's
// terminal are each left alone.
TEST_CASE("The silent-keyframe sweep takes a silent end statement", "[core][chart]")
{
    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = 1};
    note.string = 1;
    note.fret = 5;
    note.sustain = Fraction{2};

    SECTION("a slide-out toward the fret in force")
    {
        setSlideOut(note, 5);
        CHECK(stripSilentKeyframes(note));
        CHECK(note.keyframes.empty());
        // Only the statement goes: the tail simply ends where it ended.
        CHECK(note.sustain == Fraction{2});
        CHECK_FALSE(stripSilentKeyframes(note));
    }
    SECTION("a slide-out toward a fret an earlier junction reached")
    {
        // "The fret in force" is the PATH's answer, so a junction's fret counts exactly as the
        // onset's own does — and the junction itself, which travels, stays.
        note.keyframes = {Keyframe{.offset = Fraction{1}, .fret = 7, .bend = {}, .vibrato = {}}};
        setSlideOut(note, 7);
        CHECK(stripSilentKeyframes(note));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{1});
    }
    SECTION("a slide-out that travels stays")
    {
        setSlideOut(note, 3);
        CHECK_FALSE(stripSilentKeyframes(note));
        CHECK(endStatedFretOrNull(note) != nullptr);
    }
    SECTION("a scrape's terminal can never be taken")
    {
        // A scrape's whole path must TRAVEL, so its terminal states a fret the path does not hold
        // — the gesture's required exit is unreachable by this rule by construction.
        note.attack = NoteAttack::PickSlide;
        note.keyframes = {Keyframe{.offset = Fraction{1}, .fret = 9, .bend = {}, .vibrato = {}}};
        setSlideOut(note, 12);
        CHECK_FALSE(stripSilentKeyframes(note));
        CHECK(endStatedFretOrNull(note) != nullptr);
    }
}

// Every strip arm the normalizer owns works per CHANNEL. A rule that refuses a glide has nothing
// to say about a bend or vibrato authored at the same instant, and forgetting them because they
// shared an offset with the statement it refused would delete data no rule ever judged — which is
// exactly the shearing the one-array model exists to prevent.
TEST_CASE("Chart normalization strips channels, not whole keyframes", "[core][chart]")
{
    ChartTuning tuning;
    tuning.strings = {"E2"};

    const auto note_with = [](const std::vector<Keyframe>& keyframes) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = 5;
        note.sustain = Fraction{1};
        note.keyframes = keyframes;
        return note;
    };

    SECTION("the capo floor takes the fret and leaves the bend stated at the same instant")
    {
        ChartTuning capo_tuning = tuning;
        capo_tuning.capo = 3;
        ChartNote note = note_with({Keyframe{.offset = Fraction{1, 2}, .fret = 2, .bend = 1.0}});
        note.fret = 7;
        const std::vector<ChartRepair> repairs = normalizeChartNote(note, capo_tuning);
        REQUIRE(repairs.size() == 1);
        CHECK(repairs.front() == ChartRepair::FretBelowCapo);
        REQUIRE(note.keyframes.size() == 1);
        CHECK_FALSE(note.keyframes[0].fret.has_value());
        const std::optional<double>& kept_bend = note.keyframes[0].bend;
        REQUIRE(kept_bend.has_value());
        if (kept_bend.has_value())
        {
            CHECK(std::is_eq(*kept_bend <=> 1.0));
        }

        // The discriminating twin: the same below-floor fret with nothing else stated leaves its
        // keyframe bare — the strip drops nothing — and the commit law then sweeps it as silent.
        ChartNote bare = note_with({Keyframe{.offset = Fraction{1, 2}, .fret = 2}});
        bare.fret = 7;
        CHECK(normalizeChartNote(bare, capo_tuning).size() == 1);
        REQUIRE(bare.keyframes.size() == 1);
        CHECK(keyframeStatesNothing(bare.keyframes[0]));
        CHECK(stripSilentKeyframes(bare));
        CHECK(bare.keyframes.empty());
    }

    SECTION("an open string loses its path and keeps its vibrato")
    {
        ChartNote note = note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7, .vibrato = VibratoState::Narrow}});
        note.fret = 0;
        setSlideOut(note, 9);
        const std::vector<ChartRepair> repairs = normalizeChartNote(note, tuning);
        REQUIRE(repairs.size() == 1);
        CHECK(repairs.front() == ChartRepair::OpenStringSlide);
        // The slide-out went with the rest of the path: its keyframe stated a fret and nothing
        // else, so it goes with that fret — bare at the ring's end, where no leg begins, it is
        // nothing. The mid-ring keyframe keeps its width.
        CHECK(endStatedFretOrNull(note) == nullptr);
        REQUIRE(note.keyframes.size() == 1);
        CHECK_FALSE(note.keyframes[0].fret.has_value());
        CHECK(note.keyframes[0].vibrato == VibratoState::Narrow);
    }

    SECTION("a keyframe that says nothing the path does not already say is dropped")
    {
        // THE KEYFRAME COMMIT LAW's load half. The leg from 5 to 9 over one beat passes through 7
        // at the halfway mark, so a point stating 7 there changes nothing and goes; the hold
        // boundary stating 5 a quarter in — the path was travelling and now waits — says
        // something and stays, as does the arrival.
        Chart chart;
        chart.tuning = tuning;
        chart.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 4}, .fret = 5},
             Keyframe{.offset = Fraction{5, 8}, .fret = 7},
             Keyframe{.offset = Fraction{1}, .fret = 9}})};
        // With the hold boundary in place the leg runs 5 -> 9 over the remaining three quarters,
        // which passes through 7 at half a beat past the boundary: 1/4 + 3/8 = 5/8. The point is
        // LEGAL to the per-note rules — it is authoring state in memory — so only the whole-chart
        // load normalizer sheds it, and the validator, which mirrors the per-note normalizer,
        // accepts the note as it stands.
        CHECK(validateChartNoteAlone(chart.notes.front(), tuning, makeTempoMap()).has_value());
        CHECK(normalizeChartNote(chart.notes.front(), tuning).empty());
        // The document writer sheds the same point, so a saved chart never carries one — while the
        // saved FORM keeps it, because every derivation and both surfaces read through that.
        CHECK(savedChartNote(chart.notes.front()).keyframes.size() == 3);
        const Chart written = documentChart(chart, makeTempoMap());
        REQUIRE(written.notes.size() == 1);
        CHECK(written.notes.front().keyframes.size() == 2);
        const std::vector<ChartConversion> conversions = normalizeChart(chart, makeTempoMap());
        REQUIRE(conversions.size() == 1);
        CHECK(conversions.front().repair == ChartRepair::SilentKeyframe);
        REQUIRE(chart.notes.front().keyframes.size() == 2);
        CHECK(chart.notes.front().keyframes[0].offset == Fraction{1, 4});
        CHECK(chart.notes.front().keyframes[1].offset == Fraction{1});
        // Vibrato or a push that CHANGES something is a statement whatever the fret says.
        Chart vibrating;
        vibrating.tuning = tuning;
        vibrating.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{1}, .fret = 9}})};
        CHECK(normalizeChart(vibrating, makeTempoMap()).empty());
    }

    SECTION("a bend value on a flat stretch of the curve says nothing")
    {
        // The shape every Guitar Pro bend imports as: a rise, a plateau, and a trailing repeat.
        // The plateau's END (3/4) is a statement — without it the curve would fall from 1/4
        // straight to the slide-out at 1 — while its interior point (1/2) repeats both neighbours
        // and the trailing point past the slide-out repeats the value the curve then holds.
        Chart chart;
        chart.tuning = tuning;
        chart.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 4}, .bend = 1.0},
             Keyframe{.offset = Fraction{1, 2}, .bend = 1.0},
             Keyframe{.offset = Fraction{3, 4}, .bend = 1.0},
             Keyframe{.offset = Fraction{7, 8}, .bend = 0.0},
             Keyframe{.offset = Fraction{1}, .bend = 0.0}})};
        const std::vector<ChartConversion> conversions = normalizeChart(chart, makeTempoMap());
        REQUIRE(conversions.size() == 1);
        CHECK(conversions.front().repair == ChartRepair::SilentKeyframe);
        REQUIRE(chart.notes.front().keyframes.size() == 3);
        CHECK(chart.notes.front().keyframes[0].offset == Fraction{1, 4});
        CHECK(chart.notes.front().keyframes[1].offset == Fraction{3, 4});
        CHECK(chart.notes.front().keyframes[2].offset == Fraction{7, 8});
        // A point on a SLOPED segment is kept even where it is collinear: the flat case is the
        // only one judged, because bend values are doubles and no approximate test may strip a
        // statement.
        Chart sloped;
        sloped.tuning = tuning;
        sloped.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 2}, .bend = 0.5},
             Keyframe{.offset = Fraction{1}, .bend = 1.0}})};
        CHECK(normalizeChart(sloped, makeTempoMap()).empty());
    }

    SECTION("a vibrato width equal to the leg before it says nothing")
    {
        // With the point at 1/2 gone, the `Narrow` leg begun at 1/4 covers its stretch at the same
        // width, so nothing changes: beside a fret the path already holds there the point goes,
        // and the step to `Wide` is a statement and stays.
        Chart chart;
        chart.tuning = tuning;
        chart.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 4}, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{1, 2}, .fret = 5, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{3, 4}, .vibrato = VibratoState::Wide}})};
        const std::vector<ChartConversion> conversions = normalizeChart(chart, makeTempoMap());
        REQUIRE(conversions.size() == 1);
        CHECK(conversions.front().repair == ChartRepair::SilentKeyframe);
        REQUIRE(chart.notes.front().keyframes.size() == 2);
        CHECK(chart.notes.front().keyframes[0].offset == Fraction{1, 4});
        CHECK(chart.notes.front().keyframes[1].offset == Fraction{3, 4});
        // Alone, the repeated width is silent too: legal in memory, swept on load like any
        // silent point, never refused.
        Chart repeated;
        repeated.tuning = tuning;
        repeated.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 4}, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{1, 2}, .vibrato = VibratoState::Narrow}})};
        CHECK(validateChartRules(repeated, makeTempoMap()).has_value());
        const std::vector<ChartConversion> swept = normalizeChart(repeated, makeTempoMap());
        REQUIRE(swept.size() == 1);
        CHECK(swept.front().repair == ChartRepair::SilentKeyframe);
        REQUIRE(repeated.notes.front().keyframes.size() == 1);
        CHECK(repeated.notes.front().keyframes[0].offset == Fraction{1, 4});
        CHECK(validateChartRules(repeated, makeTempoMap()).has_value());
        // Judged per channel: a repeated width beside a fret the path does not pass through is a
        // statement, and the keyframe stays whole. The vibrato begins at a stop, on the hold the
        // glide to 9 leaves from, since no vibrato change may stand mid-travel.
        Chart stepped;
        stepped.tuning = tuning;
        stepped.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 4}, .fret = 5, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{1, 2}, .fret = 9, .vibrato = VibratoState::Narrow}})};
        CHECK(normalizeChart(stepped, makeTempoMap()).empty());
    }

    SECTION("a bare keyframe after a vibrated leg ends the vibrato and is kept")
    {
        // The stored form of vibrato ending where nothing else changes: the point states no
        // channel, but it begins an UNVIBRATED leg after a vibrated one, and without it the
        // onset's leg would vibrate to the ring's end.
        ChartNote ending = note_with({Keyframe{.offset = Fraction{1, 2}}});
        ending.vibrato = VibratoState::Narrow;
        CHECK_FALSE(stripSilentKeyframes(ending));
        REQUIRE(ending.keyframes.size() == 1);
        CHECK_FALSE(ending.keyframes[0].fret.has_value());
        CHECK_FALSE(ending.keyframes[0].bend.has_value());
        CHECK(ending.keyframes[0].vibrato == VibratoState::None);
        Chart chart;
        chart.tuning = tuning;
        chart.notes = {ending};
        const Chart written = documentChart(chart, makeTempoMap());
        REQUIRE(written.notes.size() == 1);
        CHECK(written.notes.front().keyframes.size() == 1);
        CHECK(normalizeChart(chart, makeTempoMap()).empty());
        REQUIRE(chart.notes.front().keyframes.size() == 1);
        CHECK(validateChartRules(chart, makeTempoMap()).has_value());

        // The discriminating twin: after an unvibrated leg the same point says nothing. It is
        // still legal, but silent — the sweep takes it, the load reports it, the writer sheds it.
        Chart plain;
        plain.tuning = tuning;
        plain.notes = {note_with({Keyframe{.offset = Fraction{1, 2}}})};
        CHECK(validateChartRules(plain, makeTempoMap()).has_value());
        ChartNote swept_note = plain.notes.front();
        CHECK(stripSilentKeyframes(swept_note));
        CHECK(swept_note.keyframes.empty());
        const Chart plain_written = documentChart(plain, makeTempoMap());
        REQUIRE(plain_written.notes.size() == 1);
        CHECK(plain_written.notes.front().keyframes.empty());
        const std::vector<ChartConversion> conversions = normalizeChart(plain, makeTempoMap());
        REQUIRE(conversions.size() == 1);
        CHECK(conversions.front().repair == ChartRepair::SilentKeyframe);
        REQUIRE(plain.notes.size() == 1);
        CHECK(plain.notes.front().keyframes.empty());
        CHECK(validateChartRules(plain, makeTempoMap()).has_value());
    }

    SECTION("a slide-out states its fret and nothing else")
    {
        // Vibrato stated where the string is let go has no ring to sound in, so the load sheds
        // it and says so; the slide-out itself stays.
        ChartNote note = note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7, .vibrato = VibratoState::Narrow}});
        note.sustain = Fraction{1, 2};
        const std::vector<ChartRepair> repairs = normalizeChartNote(note, tuning);
        REQUIRE(repairs.size() == 1);
        CHECK(repairs.front() == ChartRepair::EndStatementVibrato);
        REQUIRE(note.keyframes.size() == 1);
        CHECK_FALSE(hasVibrato(note.keyframes[0].vibrato));
        const int* const slide_out = endStatedFretOrNull(note);
        REQUIRE(slide_out != nullptr);
        if (slide_out != nullptr)
        {
            CHECK(*slide_out == 7);
        }
    }

    SECTION("a dead note loses its modulation and keeps travelling")
    {
        // A dragged mute is exactly a dead string that travels, so the position channel stays
        // while the two channels a damped string cannot sound are stripped.
        ChartNote note = note_with({Keyframe{
            .offset = Fraction{1, 2}, .fret = 7, .bend = 1.0, .vibrato = VibratoState::Narrow
        }});
        note.dead = true;
        note.vibrato = VibratoState::Narrow;
        note.bend = 2.0;
        const std::vector<ChartRepair> repairs = normalizeChartNote(note, tuning);
        REQUIRE(repairs.size() == 1);
        CHECK(repairs.front() == ChartRepair::DeadNoteModulation);
        CHECK_FALSE(hasVibrato(note.vibrato));
        CHECK(std::is_eq(note.bend <=> 0.0));
        REQUIRE(note.keyframes.size() == 1);
        const std::optional<int>& kept_fret = note.keyframes[0].fret;
        REQUIRE(kept_fret.has_value());
        if (kept_fret.has_value())
        {
            CHECK(*kept_fret == 7);
        }
        CHECK_FALSE(note.keyframes[0].bend.has_value());
        CHECK_FALSE(hasVibrato(note.keyframes[0].vibrato));
    }

    SECTION("a dead note's vibrato ending is swept once the vibrato goes, and the chart loads")
    {
        // The repair clears the onset's width, which leaves the bare ending after an UNVIBRATED
        // leg: silent, so the commit law sweeps it rather than any rule refusing the note.
        ChartNote note = note_with({Keyframe{.offset = Fraction{1, 2}}});
        note.dead = true;
        note.vibrato = VibratoState::Narrow;
        Chart chart;
        chart.tuning = tuning;
        chart.notes = {note};
        const std::vector<ChartConversion> conversions = normalizeChart(chart, makeTempoMap());
        REQUIRE(conversions.size() == 2);
        CHECK(conversions[0].repair == ChartRepair::DeadNoteModulation);
        CHECK(conversions[1].repair == ChartRepair::SilentKeyframe);
        REQUIRE(chart.notes.size() == 1);
        CHECK_FALSE(hasVibrato(chart.notes.front().vibrato));
        CHECK(chart.notes.front().keyframes.empty());
        CHECK(validateChartRules(chart, makeTempoMap()).has_value());
    }

    SECTION("a saved scrape keeps its path and sheds the channels it overrides")
    {
        // savedChartNote is the memory-to-document seam: a scrape's turnarounds are pick travel,
        // so a bend or vibrato riding one is exactly as latent as the note's own and never
        // reaches the file — while the fret statements, which ARE the path, survive.
        ChartNote note = note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 9, .bend = 1.0},
             Keyframe{.offset = Fraction{3, 4}, .vibrato = VibratoState::Narrow}});
        note.attack = NoteAttack::PickSlide;
        setSlideOut(note, 12);
        const ChartNote saved = savedChartNote(note);
        // The turnaround and the terminal survive — both are fret statements, which are the path —
        // while the vibrato-only keyframe is left bare after an unvibrated leg: silent, so the
        // commit law's sweep takes it.
        REQUIRE(saved.keyframes.size() == 3);
        const std::optional<int>& path_fret = saved.keyframes[0].fret;
        REQUIRE(path_fret.has_value());
        if (path_fret.has_value())
        {
            CHECK(*path_fret == 9);
        }
        CHECK_FALSE(saved.keyframes[0].bend.has_value());
        CHECK(keyframeStatesNothing(saved.keyframes[1]));
        CHECK(endStatedFretOrNull(saved) != nullptr);
        ChartNote swept = saved;
        CHECK(stripSilentKeyframes(swept));
        REQUIRE(swept.keyframes.size() == 2);
        CHECK(swept.keyframes[1].offset == saved.keyframes[2].offset);
        // In memory the latents are untouched, which is what makes toggling the attack back
        // restore them.
        CHECK(note.keyframes.size() == 3);
    }
}

// Verifies the single chart version gate refuses a missing or unsupported formatVersion.
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

// Verifies malformed elements, wrong-typed properties, a missing ring and out-of-order notes each
// fail the read rather than loading as something else.
TEST_CASE("Chart document rejects malformed elements", "[core][chart]")
{
    CHECK_FALSE(parseChartDocument("not json").has_value());
    CHECK_FALSE(
        parseChartDocument(R"({ "formatVersion": 1, "tuning": { "strings": 3 } })").has_value());
    CHECK_FALSE(parseChartDocument(
                    R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                    R"( "notes": [ { "position": "bad" } ] })")
                    .has_value());
    CHECK_FALSE(parseChartDocument(
                    R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                    R"( "notes": [ { "position": "1:1", "string": 1, "fret": 0, "sustain": "1/8",)"
                    R"( "attack": "chug" } ] })")
                    .has_value());
    CHECK_FALSE(parseChartDocument(
                    R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                    R"( "notes": [ { "position": "1:1", "string": 1, "fret": 0, "sustain": "1/8",)"
                    R"( "bend": [[0, 1]] } ] })")
                    .has_value());
    // A keyframe must carry a parseable offset — the slide-out included, since it is one of them.
    CHECK_FALSE(parseChartDocument(
                    R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                    R"( "notes": [ { "position": "1:1", "string": 1, "fret": 3, "sustain": "1/8",)"
                    R"( "keyframes": [ { "fret": 15 } ] } ] })")
                    .has_value());

    // A property of the WRONG TYPE is malformed, not absent. A lenient fallback would silently
    // change the music and the note would then validate clean, so nothing downstream could notice:
    // a numeric sustain read as no tail, a numeric attack as a plain pick, a numeric mute flag as
    // unmuted, a string fret as fret -1 (which validation does catch, unlike the rest), and a
    // string bend height as a flat zero-semitone bend.
    const auto parse_note = [](const std::string& note_body) {
        return parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] }, "notes": [ { )" + note_body +
            R"( } ] })");
    };
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 0, "sustain": 2)").has_value());
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 0, "sustain": "1/8", "attack": 7)")
            .has_value());
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 0, "sustain": "1/8", "palmMute": 1)")
            .has_value());
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 0, "sustain": "1/8", "dead": 1)")
            .has_value());
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": "3", "sustain": "1/8")").has_value());
    CHECK_FALSE(
        parse_note(
            R"("position": "1:1", "string": 1, "fret": 3, "sustain": "1", "bend": [["0", "half"]])")
            .has_value());
    // And the well-typed forms of the same fields still load, so the check refuses types rather
    // than fields.
    CHECK(parse_note(R"("position": "1:1", "string": 1, "fret": 0, "sustain": "2")").has_value());
    CHECK(parse_note(
              R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "attack": "tap")")
              .has_value());
    CHECK(parse_note(
              R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "palmMute": true)")
              .has_value());
    CHECK(parse_note(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "dead": true)")
              .has_value());

    // The ring is REQUIRED, and absence is malformed rather than "no tail": reading a missing key
    // as zero would invent the one datum the model cannot derive.
    const auto missing = parse_note(R"("position": "1:1", "string": 1, "fret": 5)");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ChartErrorCode::MalformedDocument);
    CHECK(missing.error().message.find("sustain") != std::string::npos);
    // And the remedy is IN that message, because a document missing the key is out of date rather
    // than merely short.
    CHECK(missing.error().message.find("re-import") != std::string::npos);
    // The requirement is the note's, not the plain pick's: every attack token the vocabulary has
    // must state a ring, so no spelling gets a document form that elides it.
    for (const std::string_view token :
         {"legato", "pinch", "leftTap", "tap", "pop", "slap", "pickSlide"})
    {
        const auto without_ring = parse_note(
            R"("position": "1:1", "string": 1, "fret": 5, "attack": ")" + std::string{token} +
            R"(")");
        REQUIRE_FALSE(without_ring.has_value());
        CHECK(without_ring.error().message.find("sustain") != std::string::npos);
    }

    // Order is a document rule as well, refused rather than repaired (the persisted sections'
    // posture): everything between the reader and the validator — the same-string bound's binary
    // search, presentation's onset grouping, the resolver's forward walk — reads the stream as a
    // sorted one, so an out-of-order document must not reach them.
    const auto unsorted = parseChartDocument(
        R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] }, "notes": [ )"
        R"({ "position": "2:1", "string": 1, "fret": 5, "sustain": "1/8" }, )"
        R"({ "position": "1:1", "string": 1, "fret": 3, "sustain": "1/8" } ] })");
    REQUIRE_FALSE(unsorted.has_value());
    CHECK(unsorted.error().code == ChartErrorCode::MalformedDocument);
    CHECK(unsorted.error().message.find("sorted") != std::string::npos);

    // "none" is a token the vocabulary does not contain, and it is refused through the ONE unknown
    // attack path every other misspelling takes — no branch of its own, so it fails exactly as
    // `"attack": "wobble"` does.
    const auto none_attack = parse_note(
        R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1", "attack": "none")");
    REQUIRE_FALSE(none_attack.has_value());
    CHECK(none_attack.error().code == ChartErrorCode::MalformedDocument);
    CHECK(
        none_attack.error().message.find("chart note attack is unknown: none") !=
        std::string::npos);
}

// Emphasis is one axis with a never-written default, so three things have to hold together: both
// named values load, the default is spelled by ABSENCE, and the removed `accent` bool is refused
// loudly rather than ignored — a silently dropped "accent" would strip every accent in the corpus
// on the next save.
TEST_CASE("Chart document reads the emphasis axis", "[core][chart]")
{
    const auto parse_note = [](const std::string& note_body) {
        return parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] }, "notes": [ { )" + note_body +
            R"( } ] })");
    };
    const auto emphasis_of = [&](const std::string& note_body) {
        const auto parsed = parse_note(note_body);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->notes.size() == 1);
        return parsed->notes.front().emphasis;
    };

    CHECK(
        emphasis_of(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8")") ==
        NoteEmphasis::Normal);
    CHECK(
        emphasis_of(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8",)"
            R"( "emphasis": "accent")") == NoteEmphasis::Accent);
    CHECK(
        emphasis_of(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8",)"
            R"( "emphasis": "ghost")") == NoteEmphasis::Ghost);

    // "normal" is the value absence already means, so spelling it is malformed like an explicit
    // "pick" attack, and an unknown token is a hard read error rather than a silent default.
    CHECK_FALSE(
        parse_note(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "emphasis": "normal")")
            .has_value());
    CHECK_FALSE(
        parse_note(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "emphasis": "loud")")
            .has_value());
    CHECK_FALSE(
        parse_note(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "emphasis": true)")
            .has_value());
    // The empty string is not a token either. It reads back as the same "" the absent key gives,
    // so accepting it would let a present-but-meaningless field pass as the default.
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "emphasis": "")")
            .has_value());

    // The tripwire: the removed key fails the load and names the fix, exactly as the removed
    // harmonic/touch keys do. Delete this with the tripwire.
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "accent": true)")
            .has_value());
    CHECK_FALSE(
        parse_note(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "accent": false)")
            .has_value());

    // Round trip: both named values survive a write and read, and a normal note writes no key at
    // all, so the common note's line carries nothing for it.
    Chart chart;
    chart.tuning.strings = {"E2"};
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_ring,
            .emphasis = NoteEmphasis::Accent,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 2},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_ring,
            .emphasis = NoteEmphasis::Ghost,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 3},
            .string = 1,
            .fret = 9,
            .sustain = g_fixture_ring,
            .bend = 0.0,
            .keyframes = {},
        },
    };
    const std::string text = chartDocumentText(chart, makeTempoMap());
    CHECK(text.find(R"("emphasis": "accent")") != std::string::npos);
    CHECK(text.find(R"("emphasis": "ghost")") != std::string::npos);
    CHECK(text.find(R"("emphasis": "normal")") == std::string::npos);
    const auto reparsed = parseChartDocument(text);
    REQUIRE(reparsed.has_value());
    if (reparsed.has_value())
    {
        REQUIRE(reparsed->notes.size() == 3);
        CHECK(reparsed->notes[0].emphasis == NoteEmphasis::Accent);
        CHECK(reparsed->notes[1].emphasis == NoteEmphasis::Ghost);
        CHECK(reparsed->notes[2].emphasis == NoteEmphasis::Normal);
    }
}

// Two mutes, two independent flags: the hands are doing two different things and can do them at
// once, so all four combinations must be writable and readable — the both-muted note especially,
// which is the state one exclusive mute axis could not express at all.
TEST_CASE("Chart document carries the two mutes independently", "[core][chart]")
{
    const auto parse_note = [](const std::string& note_body) {
        return parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] }, "notes": [ { )" + note_body +
            R"( } ] })");
    };
    const auto mutes_of = [&](const std::string& note_body) {
        const auto parsed = parse_note(note_body);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->notes.size() == 1);
        const ChartNote& note = parsed->notes.front();
        return std::pair{note.palm_mute, note.dead};
    };

    CHECK(
        mutes_of(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8")") ==
        std::pair{false, false});
    CHECK(
        mutes_of(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "palmMute": true)") ==
        std::pair{true, false});
    CHECK(
        mutes_of(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "dead": true)") ==
        std::pair{false, true});
    CHECK(
        mutes_of(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8",)"
            R"( "palmMute": true, "dead": true)") == std::pair{true, true});

    // The tripwire: the removed single "mute" key fails the load and names the fix, exactly as the
    // removed accent and harmonic/touch keys do. A silently ignored key would load every muted note
    // in the corpus as unmuted. Delete this with the tripwire.
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "mute": "palm")")
            .has_value());
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "mute": "full")")
            .has_value());

    // Round trip: every combination survives a write and read, and each flag is written only when
    // TRUE — the same absence-is-the-default rule the sibling "vibrato" and "tremolo" flags follow.
    Chart chart;
    chart.tuning.strings = {"E2"};
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_ring,
            .palm_mute = true,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 2},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_ring,
            .dead = true,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 3},
            .string = 1,
            .fret = 9,
            .sustain = g_fixture_ring,
            .palm_mute = true,
            .dead = true,
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 4},
            .string = 1,
            .fret = 11,
            .sustain = g_fixture_ring,
            .bend = 0.0,
            .keyframes = {},
        },
    };
    const std::string text = chartDocumentText(chart, makeTempoMap());
    const auto occurrences = [&text](const std::string_view key) {
        std::size_t count = 0;
        for (std::size_t at = text.find(key); at != std::string::npos;
             at = text.find(key, at + key.size()))
        {
            ++count;
        }
        return count;
    };
    // Two notes carry each flag (one alone, one paired), and the plain note carries neither.
    CHECK(occurrences(R"("palmMute": true)") == 2);
    CHECK(occurrences(R"("dead": true)") == 2);
    CHECK(occurrences(R"("palmMute")") == 2);
    CHECK(occurrences(R"("dead")") == 2);

    const auto reparsed = parseChartDocument(text);
    REQUIRE(reparsed.has_value());
    if (reparsed.has_value())
    {
        CHECK(*reparsed == chart);
    }
    CHECK(validateChartRules(chart, makeTempoMap()).has_value());
}

// The narrowest hand window must fit on the neck: with g_max_fret = 24 an index finger at 21 spans
// 21..24 and stands, while one at 22 wants 22..25 and is refused. The reach past four frets is
// derived from notes, which never state a fret off the board.
TEST_CASE("Chart rules bound the narrowest hand window by the neck", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart = makeFullChart();
    REQUIRE_FALSE(chart.fret_hand_positions.empty());

    chart.fret_hand_positions.back().fret = g_max_fret - g_min_fret_hand_width + 1;
    CHECK(validateChartRules(chart, tempo_map).has_value());

    chart.fret_hand_positions.back().fret = g_max_fret - g_min_fret_hand_width + 2;
    CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());
}

// The placement stream is strictly ascending: two placements at one instant leave "where the hand
// is" with two answers, so the validator refuses the repeat even though the stream is sorted.
TEST_CASE("Chart rules refuse two hand positions at one position", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart = makeFullChart();
    REQUIRE_FALSE(chart.fret_hand_positions.empty());
    chart.fret_hand_positions.push_back(
        FretHandPosition{
            .position = chart.fret_hand_positions.back().position,
            .fret = chart.fret_hand_positions.back().fret + 1,
        });

    const auto result = validateChartRules(chart, tempo_map);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ChartErrorCode::InvalidFretHandPosition);
}

// A placement's end fret is written only where it is authored, and reads back as authored; an
// absent key is the derived window.
TEST_CASE("Chart document round-trips a hand position's authored end fret", "[core][chart]")
{
    Chart chart = makeFullChart();
    REQUIRE_FALSE(chart.fret_hand_positions.empty());
    const std::string derived_text = chartDocumentText(chart, makeTempoMap());
    CHECK(derived_text.find(R"("endFret")") == std::string::npos);

    chart.fret_hand_positions.front().end_fret = chart.fret_hand_positions.front().fret + 2;
    const std::string text = chartDocumentText(chart, makeTempoMap());
    CHECK(text.find(R"("endFret")") != std::string::npos);
    const auto reparsed = parseChartDocument(text);
    REQUIRE(reparsed.has_value());
    if (reparsed.has_value())
    {
        CHECK(*reparsed == chart);
    }
}

// A placement states only its fret, so a document omitting it states no placement at all: the
// reader refuses it rather than inventing one the normalizer would then lift onto the board.
TEST_CASE("Chart document refuses a hand position without a fret", "[core][chart]")
{
    const Chart chart = makeFullChart();
    REQUIRE_FALSE(chart.fret_hand_positions.empty());
    std::string text = chartDocumentText(chart, makeTempoMap());
    const std::string fret_key =
        R"(, "fret": )" + std::to_string(chart.fret_hand_positions[0].fret);
    const std::size_t fret_at = text.find(fret_key, text.find(R"("fhps")"));
    REQUIRE(fret_at != std::string::npos);
    text.erase(fret_at, fret_key.size());

    const auto parsed = parseChartDocument(text);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code == ChartErrorCode::MalformedDocument);
}

// Verifies the structural rules no repair can express are refused, beside the forms the load
// normalizes instead.
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
    slide_past_sustain.notes[2].keyframes.back().offset = Fraction{3};
    const auto slide_result = validateChartRules(slide_past_sustain, tempo_map);
    REQUIRE_FALSE(slide_result.has_value());
    CHECK(slide_result.error().code == ChartErrorCode::InvalidNotePayload);

    // A negative fret is refused wherever it is stated, the slide-out included — there is no second
    // rule for the fret the hand leaves toward, because it is a keyframe's fret like any other.
    Chart negative_slide_out = makeFullChart();
    setSlideOut(negative_slide_out.notes[2], -1);
    const auto negative_result = validateChartRules(negative_slide_out, tempo_map);
    REQUIRE_FALSE(negative_result.has_value());
    CHECK(negative_result.error().code == ChartErrorCode::InvalidNotePayload);

    // A keyframe sitting exactly on a later onset of its string is LEGAL and nothing moves it: the
    // store holds what the hands did, and the spacing a mark needs to be seen belongs to
    // presentation alone. The ring here runs PAST the landing, so the one
    // rule a note cannot obey alone truncates it to exact adjacency — the third-beat gap, 1/3 — and
    // carries the statement to that end, where a stated fret IS the slide-out. That is the whole of
    // what the load path reports.
    Chart keyframe_on_onset = makeFullChart();
    keyframe_on_onset.notes[5].sustain = Fraction{1, 2};
    keyframe_on_onset.notes[5].keyframes = {Keyframe{.offset = Fraction{1, 3}, .fret = 5}};
    CHECK(validateChartRules(keyframe_on_onset, tempo_map).has_value());
    const std::vector<ChartConversion> moved = normalizeChart(keyframe_on_onset, tempo_map);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].repair == ChartRepair::OverlappingTail);
    CHECK(keyframe_on_onset.notes[5].sustain == Fraction{1, 3});
    REQUIRE(keyframe_on_onset.notes[5].keyframes.size() == 1);
    CHECK(keyframe_on_onset.notes[5].keyframes[0].offset == Fraction{1, 3});
    CHECK(endStatedFretOrNull(keyframe_on_onset.notes[5]) != nullptr);
    // And the normalized chart is its own fixpoint: a second pass reports nothing.
    CHECK(normalizeChart(keyframe_on_onset, tempo_map).empty());

    // Spans and postures are derived from the notes, so there is no out-of-range index or
    // mis-sized array left for a structural refusal to catch.

    // A harmonic node must name a real neck position. There is no companion node-with-no-harmonic
    // case: the node IS the harmonic, so there is no second field for it to disagree with and no
    // way to build the state to reject.
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

// The technique matrix, enforced: every forbidden combination refuses, and the allowed
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
        note.sustain = g_fixture_ring;
        return note;
    };
    const auto validate = [&tempo_map](const std::vector<ChartNote>& notes, const int capo = 0) {
        ChartTuning tuning;
        tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        tuning.capo = capo;
        return validateChartNotes(notes, tuning, tempo_map);
    };

    SECTION("an open string cannot slide")
    {
        // Nothing is pressed to travel, so a fret-0 glide or slide-out refuses, and the normalizer
        // strips the path whole — the refusal IS the fixpoint of that repair, which is what lets a
        // load repair the form the gate refuses. The keyframe the strip empties stays as a bare,
        // silent leg boundary for the commit law to sweep.
        ChartNote open_slide = make_note(1, 1, 0);
        open_slide.sustain = Fraction{1};
        open_slide.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 5}};
        CHECK_FALSE(validate({open_slide}).has_value());
        ChartNote shed_slide = open_slide;
        CHECK(
            normalizeChartNote(shed_slide, ChartTuning{}) ==
            std::vector<ChartRepair>{ChartRepair::OpenStringSlide});
        REQUIRE(shed_slide.keyframes.size() == 1);
        CHECK(keyframeStatesNothing(shed_slide.keyframes[0]));
        CHECK(validate({shed_slide}).has_value());
        CHECK(stripSilentKeyframes(shed_slide));
        CHECK(shed_slide.keyframes.empty());

        ChartNote open_exit = make_note(1, 1, 0);
        open_exit.sustain = Fraction{1};
        setSlideOut(open_exit, 5);
        CHECK_FALSE(validate({open_exit}).has_value());
        static_cast<void>(normalizeChartNote(open_exit, ChartTuning{}));
        CHECK(endStatedFretOrNull(open_exit) == nullptr);

        // The capo'd open is no different: the capo does not move.
        ChartNote capo_open = make_note(1, 1, 0);
        capo_open.sustain = Fraction{1};
        capo_open.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 5}};
        CHECK_FALSE(validate({capo_open}, 2).has_value());

        // A fretted glide is untouched.
        ChartNote fretted = make_note(1, 1, 3);
        fretted.sustain = Fraction{1};
        fretted.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 5}};
        CHECK(validate({fretted}).has_value());

        // Nor can a glide ARRIVE at the open string (same rule, other end): every stop on a
        // pitched path is a pressed position, so a keyframe at fret 0 refuses like one under
        // the capo. The importer degrades such a glide to the unpitched slide-out instead.
        ChartNote to_open = make_note(1, 1, 3);
        to_open.sustain = Fraction{1};
        to_open.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 0}};
        CHECK_FALSE(validate({to_open}).has_value());
    }

    SECTION("every fret a slide gesture names sits above the capo")
    {
        // W9-J: unpitched travel is travel along the SOUNDING string, so a scrape's start, its
        // turnarounds, and every slide-out's exit obey the same floor a pressed stop does — a
        // scrape at the nut is no scrape.
        const auto make_scrape = [&make_note](const int start) {
            ChartNote scrape = make_note(1, 1, start);
            scrape.attack = NoteAttack::PickSlide;
            scrape.sustain = Fraction{1};
            scrape.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 9}};
            setSlideOut(scrape, 12);
            return scrape;
        };
        CHECK(validate({make_scrape(5)}).has_value());
        CHECK_FALSE(validate({make_scrape(0)}).has_value());
        CHECK_FALSE(validate({make_scrape(5)}, 5).has_value());

        ChartNote low_turnaround = make_scrape(12);
        low_turnaround.keyframes[0].fret = 2;
        CHECK(validate({low_turnaround}).has_value());
        CHECK_FALSE(validate({low_turnaround}, 2).has_value());

        ChartNote low_exit = make_scrape(12);
        setSlideOut(low_exit, 1);
        CHECK(validate({low_exit}).has_value());
        CHECK_FALSE(validate({low_exit}, 1).has_value());

        // A pitched note's unpitched slide-out exits above the capo too.
        ChartNote slide_out = make_note(1, 1, 7);
        slide_out.sustain = Fraction{1};
        setSlideOut(slide_out, 3);
        CHECK(validate({slide_out}).has_value());
        CHECK_FALSE(validate({slide_out}, 3).has_value());
    }

    SECTION("either tapping attack needs a place to strike")
    {
        // Both attacks that strike from nowhere are bound, and both rules read one note: an open
        // string with nothing pressed and no node has nowhere for a finger to land.
        ChartNote left_tap = make_note(1, 1, 0);
        left_tap.attack = NoteAttack::LeftTap;
        CHECK_FALSE(validate({left_tap}).has_value());

        ChartNote tap = make_note(1, 1, 0);
        tap.attack = NoteAttack::Tap;
        CHECK_FALSE(validate({tap}).has_value());

        // The open-string tap harmonic would strike the node itself and satisfy this rule, but the
        // tapped harmonic is a disabled form, so it is refused by the later rule instead.
        tap.harmonic_node = 12.0;
        const auto tapped = validate({tap});
        REQUIRE_FALSE(tapped.has_value());
        CHECK(tapped.error().message.find("not supported yet") != std::string::npos);

        left_tap.fret = 5;
        CHECK(validate({left_tap}).has_value());

        // A connection CLAIM is not bound by it: whether it strikes anything at all is the
        // resolver's answer, so an open-string claim is a legal document that plays as a pick.
        ChartNote open_claim = make_note(1, 1, 0);
        open_claim.attack = NoteAttack::Legato;
        CHECK(validate({open_claim}).has_value());
    }

    SECTION("a dead note excludes the pitch-valued payloads and keeps the positions")
    {
        // Sustainless on purpose: under E25 a dead note carries no plain tail, and the tail rules
        // have their own section below. This one is about the payloads.
        ChartNote dead = make_note(1, 1, 5);
        dead.dead = true;
        CHECK(validate({dead}).has_value());

        // A palm mute alone excludes nothing — it is still a pitched note — and adding it to a
        // dead note changes no verdict either, because the rule reads the dead flag alone.
        ChartNote palm_bend = make_note(1, 1, 5);
        palm_bend.palm_mute = true;
        palm_bend.sustain = Fraction{1};
        palm_bend.bend = 1.0;
        CHECK(validate({palm_bend}).has_value());

        ChartNote both = dead;
        both.palm_mute = true;
        CHECK(validate({both}).has_value());

        ChartNote both_bend = both;
        both_bend.bend = 1.0;
        CHECK_FALSE(validate({both_bend}).has_value());

        // A dead harmonic is LEGAL: the node is positional there, saying where the hand is rather
        // than what rings, which is the same reading that lets a dead note keep its fret. Probed
        // over the open string, since a node over a pressed stop is the disabled artificial form
        // whatever the deadening says.
        ChartNote muted_harmonic = make_note(1, 1, 0);
        muted_harmonic.dead = true;
        muted_harmonic.harmonic_node = 17.0;
        CHECK(validate({muted_harmonic}).has_value());
        // And it stays DEAD through normalization rather than being un-deadened into a sounding
        // harmonic: the deadening outranks the node, so detection and scoring see percussive.
        ChartNote executed = muted_harmonic;
        CHECK(normalizeChartNote(executed, ChartTuning{}).empty());
        CHECK(executed.dead);
        CHECK(executed.harmonic_node.has_value());

        // ...but the PINCH's node does not survive, because it is the one off the neck: it
        // records the thumb's graze rather than a hand position, and the squeal it asks for
        // cannot sound on a damped string.
        ChartNote muted_pinch = muted_harmonic;
        muted_pinch.attack = NoteAttack::Pinch;
        CHECK_FALSE(validate({muted_pinch}).has_value());
        // The normalizer drops the whole harmonic rather than the node alone, which would leave
        // a pinch with none; the deadening is what survives.
        ChartNote shed_pinch = muted_pinch;
        CHECK(
            normalizeChartNote(shed_pinch, ChartTuning{}) ==
            std::vector<ChartRepair>{ChartRepair::DeadPinch});
        CHECK(shed_pinch.dead);
        CHECK(shed_pinch.attack == NoteAttack::Pick);
        CHECK_FALSE(shed_pinch.harmonic_node.has_value());
        CHECK(validate({shed_pinch}).has_value());

        ChartNote muted_bend = dead;
        muted_bend.bend = 1.0;
        CHECK_FALSE(validate({muted_bend}).has_value());

        ChartNote muted_vibrato = dead;
        muted_vibrato.vibrato = VibratoState::Narrow;
        CHECK_FALSE(validate({muted_vibrato}).has_value());

        // Positions survive the same test: the dragged muted slide is real music — and a slide
        // is one of the two things that earn a dead note its tail (E25, below).
        ChartNote muted_slide = dead;
        muted_slide.sustain = Fraction{1};
        muted_slide.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 9}};
        CHECK(validate({muted_slide}).has_value());
    }

    SECTION("a dead note keeps its stored ring: E25 is a presentation rule, not a repair")
    {
        // E25 lives outside the stored form: a dead note's damped stroke has a duration like any
        // other, and that duration is the timing the legato adjacency test reads. What it does NOT
        // have is a drawn tail — that is chartPresentation rule 4, covered in
        // test_chart_presentation.cpp — so nothing here refuses or trims one.
        ChartNote plain_tail = make_note(1, 1, 5);
        plain_tail.dead = true;
        plain_tail.sustain = Fraction{1};
        CHECK(validate({plain_tail}).has_value());
        ChartNote unchanged = plain_tail;
        CHECK(normalizeChartNote(unchanged, ChartTuning{}).empty());
        CHECK(unchanged.sustain == Fraction{1});

        ChartNote chug = plain_tail;
        chug.tremolo = true;
        CHECK(validate({chug}).has_value());

        ChartNote dragged = plain_tail;
        setSlideOut(dragged, 3);
        CHECK(validate({dragged}).has_value());

        // The palm mute is untouched: a palm-muted note rings, so its tail is an ordinary one.
        ChartNote palm_tail = make_note(1, 1, 5);
        palm_tail.palm_mute = true;
        palm_tail.sustain = Fraction{1};
        CHECK(validate({palm_tail}).has_value());

        // A dead tap harmonic is reduced to the plain note at its stop (the tapped form is
        // disabled) — and that is the ONLY repair it takes: the shed never drags the ring away with
        // it.
        ChartNote dead_tap_harmonic = make_note(1, 1, 5);
        dead_tap_harmonic.dead = true;
        dead_tap_harmonic.attack = NoteAttack::Tap;
        dead_tap_harmonic.harmonic_node = 17.0;
        dead_tap_harmonic.tremolo = true;
        dead_tap_harmonic.sustain = Fraction{1};
        CHECK(
            normalizeChartNote(dead_tap_harmonic, ChartTuning{}) ==
            std::vector<ChartRepair>{ChartRepair::DisabledHarmonic});
        CHECK(dead_tap_harmonic.sustain == Fraction{1});
    }

    SECTION("a note must ring: a non-positive sustain is refused, never repaired")
    {
        // Structural, because no repair can invent a duration — and it doubles as the format
        // tripwire for any zero that reaches memory.
        ChartNote silent = make_note(1, 1, 5);
        silent.sustain = Fraction{};
        const auto refused = validate({silent});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ChartErrorCode::InvalidNote);
        CHECK(refused.error().message.find("re-imported") != std::string::npos);
        // Nothing in the normalizer answers it, which is what makes it a refusal.
        ChartNote normalized = silent;
        CHECK(normalizeChartNote(normalized, ChartTuning{}).empty());
        CHECK(normalized.sustain == Fraction{});

        ChartNote negative = make_note(1, 1, 5);
        negative.sustain = Fraction{-1, 4};
        CHECK_FALSE(validate({negative}).has_value());

        // EVERY attack, with no exemption for any of them: the rule reads the duration alone, and
        // it is asked before any attack-specific shape rule, so the answer is the same refusal
        // whatever the note claims to be.
        for (const NoteAttack attack :
             {NoteAttack::Pick,
              NoteAttack::Pinch,
              NoteAttack::Legato,
              NoteAttack::LeftTap,
              NoteAttack::Tap,
              NoteAttack::Pop,
              NoteAttack::Slap,
              NoteAttack::PickSlide})
        {
            ChartNote ringless = make_note(1, 1, 5);
            ringless.sustain = Fraction{};
            ringless.attack = attack;
            const auto by_attack = validate({ringless});
            REQUIRE_FALSE(by_attack.has_value());
            CHECK(by_attack.error().message.find("must be positive") != std::string::npos);
        }
    }

    SECTION("a tap harmonic cannot be tremolo picked; a picked one over a stop can")
    {
        // While the tapped form is disabled the normalizer reduces the whole harmonic to the plain
        // note at its stop before the tremolo exclusion can be asked: a picked note may be tremolo
        // picked, so the tremolo survives and the record is legal again. The exclusion itself
        // (the damping finger leaves the string, so nothing holds the node) waits behind the door.
        ChartNote tap_harmonic = make_note(1, 1, 5);
        tap_harmonic.attack = NoteAttack::Tap;
        tap_harmonic.harmonic_node = 17.0;
        tap_harmonic.tremolo = true;
        CHECK_FALSE(validate({tap_harmonic}).has_value());
        CHECK(
            normalizeChartNote(tap_harmonic, ChartTuning{}) ==
            std::vector<ChartRepair>{ChartRepair::DisabledHarmonic});
        CHECK(tap_harmonic.attack == NoteAttack::Pick);
        CHECK_FALSE(tap_harmonic.harmonic_node.has_value());
        CHECK(tap_harmonic.tremolo);
        CHECK(validate({tap_harmonic}).has_value());

        // A harmonic the picking hand damps over a pressed stop keeps the fretting finger on its
        // stop, so re-picking works; the pinch is that family's one enabled member.
        ChartNote pinch = make_note(1, 1, 5);
        pinch.attack = NoteAttack::Pinch;
        pinch.harmonic_node = 17.0;
        pinch.tremolo = true;
        CHECK(validate({pinch}).has_value());
    }

    SECTION("a fret-hand harmonic cannot slide, bend, or vibrato; one over a stop can")
    {
        ChartNote natural = make_note(1, 1, 0);
        natural.harmonic_node = 12.0;
        natural.sustain = Fraction{1};
        CHECK(validate({natural}).has_value());

        ChartNote sliding = natural;
        sliding.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 14}};
        CHECK_FALSE(validate({sliding}).has_value());

        ChartNote trailing = natural;
        setSlideOut(trailing, 9);
        CHECK_FALSE(validate({trailing}).has_value());

        ChartNote bending = natural;
        bending.bend = 1.0;
        CHECK_FALSE(validate({bending}).has_value());

        ChartNote oscillating = natural;
        oscillating.vibrato = VibratoState::Narrow;
        CHECK_FALSE(validate({oscillating}).has_value());

        // A harmonic over a real stop is the picking-hand-damped family: the fretting hand is
        // pressing, so bending it is ordinary work. The pinch is that family's one enabled member
        // while the artificial form is disabled.
        ChartNote fretted = make_note(1, 1, 5);
        fretted.attack = NoteAttack::Pinch;
        fretted.harmonic_node = 17.0;
        fretted.sustain = Fraction{1};
        fretted.bend = 1.0;
        CHECK(validate({fretted}).has_value());
    }

    SECTION("the capo floor binds notes")
    {
        CHECK(validate({make_note(1, 1, 5)}, 3).has_value());
        CHECK(validate({make_note(1, 1, 0)}, 3).has_value());
        CHECK_FALSE(validate({make_note(1, 1, 2)}, 3).has_value());
        CHECK_FALSE(validate({make_note(1, 1, 3)}, 3).has_value());
    }

    SECTION("the capo floor binds pitched glide keyframes and scrape travel alike")
    {
        ChartNote glide = make_note(1, 1, 5);
        glide.sustain = Fraction{1};
        glide.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 4}};
        CHECK(validate({glide}, 3).has_value());

        glide.keyframes.front().fret = 2;
        CHECK_FALSE(validate({glide}, 3).has_value());

        // W9-J binds a scrape's turnaround too, unpitched travel or not — the pick travels the
        // sounding string, so dipping below the capo is not a scrape. The same scrape with its
        // turnaround above the capo stands.
        ChartNote scrape = make_note(1, 1, 5);
        scrape.attack = NoteAttack::PickSlide;
        scrape.sustain = Fraction{1};
        scrape.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 1}};
        setSlideOut(scrape, 6);
        CHECK_FALSE(validate({scrape}, 3).has_value());
        scrape.keyframes.front().fret = 4;
        CHECK(validate({scrape}, 3).has_value());
    }
}

// The one normalizer: every repairable rule stated once as a repair, with the validator asking its
// fixpoint. Each case pins three things at once — the repair the normalizer applies, that the form
// it repairs is exactly the form the validator refuses, and that the repaired form validates — so
// the two can never disagree about a rule.
TEST_CASE("Chart normalizer repairs what the validator refuses, once", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    tuning.capo = 3;

    const auto make_note = [](const int beat, const int string, const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = beat};
        note.string = string;
        note.fret = fret;
        note.sustain = g_fixture_ring;
        return note;
    };
    const auto valid = [&tempo_map, &tuning](const ChartNote& note) {
        return validateChartNoteAlone(note, tuning, tempo_map).has_value();
    };
    // One pass must reach the fixpoint: a second application changes nothing.
    const auto idempotent = [&tuning](ChartNote note) {
        static_cast<void>(normalizeChartNote(note, tuning));
        return normalizeChartNote(note, tuning).empty();
    };

    SECTION("a fret, keyframe, or exit past the board clamps onto it")
    {
        ChartNote past = make_note(1, 1, 30);
        past.sustain = Fraction{1};
        past.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 27}};
        setSlideOut(past, 26);
        CHECK_FALSE(valid(past));
        CHECK(idempotent(past));
        CHECK(
            normalizeChartNote(past, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretPastBoard});
        CHECK(past.fret == g_max_fret);
        CHECK(past.keyframes.front().fret == g_max_fret);
        // The slide-out is clamped by the same keyframe rule, with no clause of its own.
        const int* const clamped_slide_out = endStatedFretOrNull(past);
        REQUIRE(clamped_slide_out != nullptr);
        if (clamped_slide_out != nullptr)
        {
            CHECK(*clamped_slide_out == g_max_fret);
        }
        CHECK(valid(past));
    }

    SECTION("slide positions on or below the capo lift above it, keyframe stops drop")
    {
        // A glide's stops are pressed positions and a scrape's turnarounds are pick travel, so
        // neither may name the open string or a capo'd fret: a PITCHED keyframe there names
        // nothing pressed and loses its fret, while the slide-out is a direction as much as a
        // fret — a slide-out toward the floor is still a slide-out — so it lifts onto the floor
        // instead.
        ChartNote glide = make_note(1, 1, 9);
        glide.sustain = Fraction{1};
        glide.keyframes = {
            Keyframe{.offset = Fraction{1, 4}, .fret = 7},
            Keyframe{.offset = Fraction{1, 2}, .fret = 2},
        };
        setSlideOut(glide, 3);
        CHECK_FALSE(valid(glide));
        CHECK(idempotent(glide));
        CHECK(
            normalizeChartNote(glide, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretBelowCapo});
        // The surviving stop and the lifted slide-out: the dropped one stated nothing else, so its
        // fret leaves it bare — a silent leg boundary the commit law sweeps.
        REQUIRE(glide.keyframes.size() == 3);
        CHECK(glide.keyframes.front().fret == 7);
        CHECK(keyframeStatesNothing(glide.keyframes[1]));
        const int* const lifted_slide_out = endStatedFretOrNull(glide);
        REQUIRE(lifted_slide_out != nullptr);
        if (lifted_slide_out != nullptr)
        {
            CHECK(*lifted_slide_out == tuning.capo + 1);
        }
        CHECK(valid(glide));
        CHECK(stripSilentKeyframes(glide));
        CHECK(glide.keyframes.size() == 2);

        // A scrape has no open form, so its START lifts too — the pick travels the sounding
        // string, and a scrape at the nut is no scrape.
        ChartNote scrape = make_note(1, 1, 0);
        scrape.attack = NoteAttack::PickSlide;
        scrape.sustain = Fraction{1};
        setSlideOut(scrape, 12);
        CHECK_FALSE(valid(scrape));
        CHECK(
            normalizeChartNote(scrape, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretBelowCapo});
        CHECK(scrape.fret == tuning.capo + 1);
        CHECK(scrape.attack == NoteAttack::PickSlide);
        CHECK(valid(scrape));

        // A PRESSED note on a capo'd fret has no repair that is not an invented pitch: it stays a
        // refusal, and the normalizer leaves it alone.
        ChartNote pressed_low = make_note(1, 1, 2);
        CHECK_FALSE(valid(pressed_low));
        CHECK(normalizeChartNote(pressed_low, tuning).empty());
    }

    SECTION("a scrape whose path no longer travels becomes the plain pick it sounds like")
    {
        // Stilled by the repairs that came before it: the exit lifts onto the start.
        ChartNote scrape = make_note(1, 1, 4);
        scrape.attack = NoteAttack::PickSlide;
        scrape.sustain = Fraction{1};
        setSlideOut(scrape, 1);
        CHECK_FALSE(valid(scrape));
        CHECK(idempotent(scrape));
        CHECK(
            normalizeChartNote(scrape, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretBelowCapo, ChartRepair::StilledScrape});
        CHECK(scrape.attack == NoteAttack::Pick);
        // The terminal's fret is gone and its keyframe with it: bare at the ring's end, where no
        // leg begins, it is nothing.
        CHECK(scrape.keyframes.empty());
        CHECK(endStatedFretOrNull(scrape) == nullptr);
        CHECK(scrape.sustain == Fraction{1});
        CHECK(valid(scrape));

        // And stilled on its own: a turnaround that sits where the start is.
        ChartNote stilled = make_note(1, 1, 12);
        stilled.attack = NoteAttack::PickSlide;
        stilled.sustain = Fraction{1};
        stilled.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 12}};
        setSlideOut(stilled, 5);
        CHECK_FALSE(valid(stilled));
        CHECK(
            normalizeChartNote(stilled, tuning) ==
            std::vector<ChartRepair>{ChartRepair::StilledScrape});
        CHECK(stilled.attack == NoteAttack::Pick);
        CHECK(valid(stilled));

        // A scrape with no terminal at all is missing data, not a stilled gesture: the travel
        // test never runs, and the whole-stream validator refuses it.
        ChartNote no_terminal = make_note(1, 1, 12);
        no_terminal.attack = NoteAttack::PickSlide;
        no_terminal.sustain = Fraction{1};
        CHECK(normalizeChartNote(no_terminal, tuning).empty());
        CHECK_FALSE(validateChartNotes({no_terminal}, tuning, tempo_map).has_value());
    }

    SECTION("a strike with nowhere to land becomes a plain pick")
    {
        ChartNote stranded = make_note(1, 1, 0);
        stranded.attack = NoteAttack::LeftTap;
        CHECK_FALSE(valid(stranded));
        CHECK(
            normalizeChartNote(stranded, tuning) ==
            std::vector<ChartRepair>{ChartRepair::StrandedStrike});
        CHECK(stranded.attack == NoteAttack::Pick);
        CHECK(valid(stranded));
    }

    SECTION("a hand window fits onto the playable board")
    {
        // Index finger on a capo'd fret: lifts above the capo.
        FretHandPosition low{.position = GridPosition{.measure = 1, .beat = 1}, .fret = 2};
        CHECK(
            normalizeFretHandPosition(low, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretBelowCapo});
        CHECK(low.fret == tuning.capo + 1);
        CHECK(normalizeFretHandPosition(low, tuning).empty());

        // Running off the end: drops until the narrowest window fits under the last fret.
        FretHandPosition high{.position = GridPosition{.measure = 1, .beat = 1}, .fret = 23};
        CHECK(
            normalizeFretHandPosition(high, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretPastBoard});
        CHECK(high.fret == g_max_fret - g_min_fret_hand_width + 1);
        CHECK(normalizeFretHandPosition(high, tuning).empty());

        // An authored end past the last fret comes down onto it.
        FretHandPosition reaching{
            .position = GridPosition{.measure = 1, .beat = 1},
            .fret = 20,
            .end_fret = g_max_fret + 2,
        };
        CHECK(
            normalizeFretHandPosition(reaching, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretPastBoard});
        CHECK(reaching.fret == 20);
        CHECK(reaching.end_fret == std::optional{g_max_fret});
        CHECK(normalizeFretHandPosition(reaching, tuning).empty());
    }

    SECTION("the whole chart normalizes in one call, with the settle sweep last")
    {
        // Every stage in one call, in order: the stream-level ring bound after the
        // per-note repairs, the hand windows, and the relational sweep LAST — over the stream as
        // it will actually stand. Each repair is reported with its place, which is what the load
        // notice shows.
        Chart chart;
        chart.tuning = tuning;
        // String 2 rings two bars through its own restrike: the truncation bounds it at that
        // onset.
        ChartNote overlapping = make_note(1, 2, 9);
        overlapping.sustain = Fraction{8};
        const ChartNote restrike = make_note(3, 2, 7);
        // String 1 claims a connection its predecessor's eighth-of-a-beat ring cannot reach.
        const ChartNote released = make_note(1, 1, 9);
        ChartNote claim = make_note(2, 1, 5);
        claim.attack = NoteAttack::Legato;
        chart.notes = {released, overlapping, claim, restrike};
        chart.fret_hand_positions.push_back(
            FretHandPosition{.position = GridPosition{.measure = 1, .beat = 1}, .fret = 1});
        CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());

        const std::vector<ChartConversion> conversions = normalizeChart(chart, tempo_map);
        REQUIRE(conversions.size() == 3);
        CHECK(conversions[0].repair == ChartRepair::OverlappingTail);
        CHECK(conversions[0].where == "1:1 string 2");
        CHECK(conversions[1].repair == ChartRepair::FretBelowCapo);
        CHECK(conversions[1].where == "hand position 1:1");
        CHECK(conversions[2].repair == ChartRepair::UnjustifiedLegato);
        CHECK(conversions[2].where == "1:2 string 1");
        // The overlapping ring ends exactly on its string's restrike, two beats on.
        CHECK(chart.notes[1].sustain == Fraction{2});
        CHECK(chart.notes[2].attack == NoteAttack::Pick);
        CHECK(validateChartRules(chart, tempo_map).has_value());

        // Normal already: a second call reports nothing, which is what a caller tests to know
        // whether memory still equals disk.
        CHECK(normalizeChart(chart, tempo_map).empty());

        // And the user-facing sentence is spelled once, for logs and notices alike.
        CHECK(
            chartConversionText(conversions[0]) ==
            std::string{chartRepairText(ChartRepair::OverlappingTail)} + " at 1:1 string 2");
    }
}

// THE SHIFT SLIDE AS A FACT THE CHART PROVES. A fret at a ring's end is one statement and two
// opposite gestures, and the five clauses of arrivesIntoNextHead are the whole of what tells them
// apart. Each is pinned in both directions, because a clause that only ever answers one way is a
// clause nothing depends on. Asked through the CONNECTIONS, the path every consumer takes.
TEST_CASE("A shift slide is the arrival the chart proves", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    // Two notes a beat apart on one string, the first ringing exactly to the second: the shape
    // every clause below varies one field of. Returns `bool`, never a deduced type: an element of a
    // std::vector<bool> is a proxy into `connections`, which dies at the return.
    const auto pair_with = [&tempo_map](const ChartNote& first, const ChartNote& second) -> bool {
        const std::vector<ChartNote> notes{first, second};
        const ChartConnections connections = chartConnections(notes, tempo_map);
        REQUIRE(connections.arrives_into.size() == 2);
        // The relation is written from the SUCCESSOR onto its predecessor, and nothing follows the
        // second note, so its own entry is always false.
        CHECK_FALSE(connections.arrives_into[1]);
        return connections.arrives_into[0];
    };
    const auto glide_to = [](const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 3;
        note.fret = 5;
        note.sustain = Fraction{1};
        setSlideOut(note, fret);
        return note;
    };
    const auto head_at = [](const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 2};
        note.string = 3;
        note.fret = fret;
        note.sustain = Fraction{1};
        return note;
    };

    SECTION("the whole shape: the end names the stop the next head takes, at the same instant")
    {
        CHECK(pair_with(glide_to(9), head_at(9)));
    }
    SECTION("clause 1: an end that names no fret states no gesture to classify")
    {
        ChartNote bends_out = glide_to(9);
        clearSlideOut(bends_out);
        static_cast<void>(setEndStatement(
            bends_out, Keyframe{.offset = {}, .fret = {}, .bend = 1.0, .vibrato = {}}));
        CHECK_FALSE(pair_with(bends_out, head_at(9)));
    }
    SECTION("clause 2: adjacency is exact, and a slide-out that merely abuts is a slide-out")
    {
        ChartNote falls_short = glide_to(9);
        falls_short.sustain = Fraction{1, 2};
        falls_short.keyframes.back().offset = Fraction{1, 2};
        CHECK_FALSE(pair_with(falls_short, head_at(9)));
        // And a ring running PAST the head is something else again; the same-string clamp cuts it
        // before any pair exists, but the relation refuses it on its own terms.
        ChartNote runs_past = glide_to(9);
        runs_past.sustain = Fraction{2};
        runs_past.keyframes.back().offset = Fraction{2};
        CHECK_FALSE(pair_with(runs_past, head_at(9)));
    }
    SECTION("clause 3: a scrape on either side never arrives")
    {
        ChartNote scrape = glide_to(9);
        scrape.attack = NoteAttack::PickSlide;
        CHECK_FALSE(pair_with(scrape, head_at(9)));
        ChartNote scraped_into = head_at(9);
        scraped_into.attack = NoteAttack::PickSlide;
        setSlideOut(scraped_into, 12);
        CHECK_FALSE(pair_with(glide_to(9), scraped_into));
    }
    SECTION("clause 4: a next head the PICKING hand stops is not arrived into")
    {
        ChartNote tapped = head_at(9);
        tapped.attack = NoteAttack::Tap;
        CHECK_FALSE(pair_with(glide_to(9), tapped));
        // A TAPPED HARMONIC is the exception, and it falls out of the same predicate rather than
        // needing a clause: the fretting hand holds the stop its node rides, which is that note's
        // own fret. Pinned at the relation's level, validation refusing tapped harmonics.
        ChartNote tapped_harmonic = tapped;
        tapped_harmonic.harmonic_node = 12.0;
        CHECK(pair_with(glide_to(9), tapped_harmonic));
    }
    SECTION("clause 5: the fret named is the stop the head is struck at, node-aware")
    {
        // A DIFFERENT fret abutting is a slide-out, which most of the corpus's slide-outs are.
        CHECK_FALSE(pair_with(glide_to(9), head_at(7)));
        // The open string can never match: a keyframe at fret 0 is refused, so there is nothing to
        // glide to.
        CHECK_FALSE(pair_with(glide_to(9), head_at(0)));
        // NODE-AWARE: a harmonic touched at node 9 is not the pressed fret 9 the glide reaches.
        ChartNote node_head = head_at(9);
        node_head.fret = 0;
        node_head.harmonic_node = 9.0;
        CHECK_FALSE(pair_with(glide_to(9), node_head));
    }
    SECTION("what is deliberately absent: whether the next head claims legato")
    {
        // The successor's own claim is its business. An equal-fret claim can never be justified, so
        // after a settle it plays as the pick it sounds like, while the ARRIVAL stands either way —
        // which is what keeps the `Shift+L` split's product from changing the relation under it.
        ChartNote claimed = head_at(9);
        claimed.attack = NoteAttack::Legato;
        CHECK(pair_with(glide_to(9), claimed));
        std::vector<ChartNote> notes{glide_to(9), claimed};
        CHECK(sweepUnjustifiedLegato(notes, tempo_map).size() == 1);
        CHECK(notes[1].attack == NoteAttack::Pick);
        // The arrival survives it: a settle changes an attack, never a stop.
        CHECK(chartConnections(notes, tempo_map).arrives_into[0]);
    }
}

// THE PAIR FACT THE SURFACES NEED: whether a ring's end lands exactly on the next head of its own
// string, which is clause 2 of the arrival on its own. It is what an ARRIVAL and an abutting
// SLIDE-OUT share — two marks at one column, whatever the gesture — so it is resolved beside the
// relation and asked of neither note alone.
TEST_CASE("The connections report a ring ending on the next head", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    // True where the first ring ends on the second, whose index the connection then names.
    const auto ends_on_next_head =
        [&tempo_map](const ChartNote& first, const ChartNote& second) -> bool {
        const std::vector<ChartNote> notes{first, second};
        const ChartConnections connections = chartConnections(notes, tempo_map);
        REQUIRE(connections.end_heads.size() == 2);
        // Written from the SUCCESSOR onto its predecessor, so nothing following means empty.
        CHECK_FALSE(connections.end_heads[1].has_value());
        const std::optional<std::size_t>& end_head = connections.end_heads[0];
        if (end_head.has_value())
        {
            CHECK(*end_head == 1);
        }
        return end_head.has_value();
    };
    ChartNote ring;
    ring.position = GridPosition{.measure = 1, .beat = 1};
    ring.string = 3;
    ring.fret = 5;
    ring.sustain = Fraction{1};
    ChartNote head;
    head.position = GridPosition{.measure = 1, .beat = 2};
    head.string = 3;
    head.fret = 9;
    head.sustain = Fraction{1};

    // The arrival and the abutting slide-out answer alike, and so does a ring stating nothing at
    // all: what shares the instant is the ring's END, not a gesture.
    CHECK(ends_on_next_head(ring, head));
    ChartNote arriving = ring;
    setSlideOut(arriving, 9);
    CHECK(ends_on_next_head(arriving, head));
    ChartNote falling = ring;
    setSlideOut(falling, 12);
    CHECK(ends_on_next_head(falling, head));

    // A ring stopping short shares no instant, and neither does one on another string.
    ChartNote short_ring = falling;
    short_ring.sustain = Fraction{1, 2};
    short_ring.keyframes.back().offset = Fraction{1, 2};
    CHECK_FALSE(ends_on_next_head(short_ring, head));
    ChartNote other_string = head;
    other_string.string = 4;
    CHECK_FALSE(ends_on_next_head(falling, other_string));
}

// AN END STATEMENT LEAVES NO VIBRATO, and keeps its BEND. The channel table's own law, note-local
// and asked of ANY end statement: vibrato stated where the string is let go has no ring to sound
// in, while a bend there is the curve's LAST value and shapes the final leg running into the end —
// as true of a slide-out as of a shift slide's arrival.
TEST_CASE("An end statement sheds its vibrato and keeps its bend", "[core][chart]")
{
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = 1};
    note.string = 3;
    note.fret = 5;
    note.sustain = Fraction{1};

    SECTION("a fret and a bend stand together at the end, whichever gesture the fret proves")
    {
        static_cast<void>(
            setEndStatement(note, Keyframe{.offset = {}, .fret = 9, .bend = 1.0, .vibrato = {}}));
        REQUIRE(note.keyframes.size() == 1);
        // Bound once, with the explicit guard the CI-only optional checker needs.
        const std::optional<double>& reached = note.keyframes[0].bend;
        CHECK(note.keyframes[0].fret == 9);
        REQUIRE(reached.has_value());
        if (reached.has_value())
        {
            CHECK_THAT(*reached, Catch::Matchers::WithinULP(1.0, 0));
        }
        // Through the normalizer, which is the load path's own half of the law.
        CHECK(normalizeChartNote(note, tuning).empty());
        // And through a clip, which carries the end's statement to the new end whole.
        static_cast<void>(clipPayloadsToSustain(note, Fraction{1, 2}));
        REQUIRE(note.keyframes.size() == 1);
        const std::optional<double>& carried = note.keyframes[0].bend;
        CHECK(note.keyframes[0].offset == Fraction{1, 2});
        CHECK(note.keyframes[0].fret == 9);
        REQUIRE(carried.has_value());
        if (carried.has_value())
        {
            CHECK_THAT(*carried, Catch::Matchers::WithinULP(1.0, 0));
        }
    }
    SECTION("vibrato at the end is shed, and a statement that said only the vibrato goes whole")
    {
        static_cast<void>(setEndStatement(
            note, Keyframe{.offset = {}, .fret = 9, .bend = {}, .vibrato = VibratoState::Narrow}));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].fret == 9);
        CHECK_FALSE(hasVibrato(note.keyframes[0].vibrato));
        // No fret beside it, so shedding the vibrato leaves nothing stated and the keyframe goes:
        // a bare keyframe at the ring's end, where no leg begins, is nothing.
        note.keyframes = {
            Keyframe{.offset = Fraction{1}, .fret = {}, .bend = {}, .vibrato = VibratoState::Wide}
        };
        CHECK(shedEndStatementVibrato(note));
        CHECK(note.keyframes.empty());
    }
}

// NO VIBRATO CHANGE MID-TRAVEL: a width may run through a glide, but none starts, stops or changes
// strictly inside one, so the shed hands such a point the width of the leg it divides. A bend there
// is legal, and so is any change at a stop or where the hand rests.
TEST_CASE("A vibrato change standing mid-travel is taken back", "[core][chart]")
{
    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = 1};
    note.string = 3;
    note.fret = 5;
    note.sustain = Fraction{4};
    const ChartTuning tuning{};

    SECTION("a vibrato start inside a glide is taken back, and the normalizer reports it")
    {
        note.keyframes = {
            Keyframe{
                .offset = Fraction{1}, .fret = {}, .bend = {}, .vibrato = VibratoState::Narrow
            },
            Keyframe{.offset = Fraction{2}, .fret = 7, .bend = {}, .vibrato = VibratoState::Narrow},
        };
        const std::vector<ChartRepair> repairs = normalizeChartNote(note, tuning);
        REQUIRE(repairs.size() == 1);
        CHECK(repairs.front() == ChartRepair::MidTravelVibrato);
        REQUIRE(note.keyframes.size() == 2);
        CHECK(note.keyframes[0].vibrato == VibratoState::None);
        // The stop keeps its own width: a change AT a stop is not mid-travel.
        CHECK(note.keyframes[1].vibrato == VibratoState::Narrow);
    }
    SECTION("a vibrato end inside the leg into a slide-out is taken back")
    {
        note.vibrato = VibratoState::Wide;
        note.keyframes = {
            Keyframe{.offset = Fraction{3}, .fret = {}, .bend = {}, .vibrato = VibratoState::None},
            Keyframe{.offset = Fraction{4}, .fret = 9, .bend = {}, .vibrato = VibratoState::None},
        };
        CHECK(shedMidTravelVibrato(note));
        CHECK(note.keyframes[0].vibrato == VibratoState::Wide);
    }
    SECTION("changes on a hold, after the last stop, and a bend mid-travel all stand")
    {
        note.keyframes = {
            // A bend alone inside the glide from 5 to 7.
            Keyframe{
                .offset = Fraction{1, 2}, .fret = {}, .bend = 1.0, .vibrato = VibratoState::None
            },
            Keyframe{.offset = Fraction{1}, .fret = 7, .bend = {}, .vibrato = VibratoState::None},
            // A hold on the 7, then a vibrato start on it: the hand rests.
            Keyframe{
                .offset = Fraction{3, 2}, .fret = 7, .bend = {}, .vibrato = VibratoState::None
            },
            Keyframe{
                .offset = Fraction{2}, .fret = {}, .bend = {}, .vibrato = VibratoState::Narrow
            },
            Keyframe{
                .offset = Fraction{5, 2}, .fret = 7, .bend = {}, .vibrato = VibratoState::Narrow
            },
            // Past the last stop the path holds, so a width step there is at rest too.
            Keyframe{.offset = Fraction{3}, .fret = {}, .bend = {}, .vibrato = VibratoState::Wide},
        };
        const std::vector<Keyframe> before = note.keyframes;
        CHECK_FALSE(shedMidTravelVibrato(note));
        CHECK(note.keyframes == before);
    }
}

// A CLIP REPORTS WHAT IT LOST. Shortening a ring may carry the end's statement back and may land it
// on a point standing at the new end; what it may not do unannounced is erase a point past that
// end, overwrite a value the standing point stated, or shed vibrato stated where the ring now
// stops, because the plan gate refuses exactly on this report (finalizePlan). The end's own writer,
// landing on a standing statement, reports an overwrite or a shed vibrato the same way.
TEST_CASE("A clip reports an erased, overwritten or shed statement", "[core][chart]")
{
    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = 1};
    note.string = 3;
    note.fret = 5;
    note.sustain = Fraction{4};

    SECTION("an end statement riding back alone loses nothing")
    {
        note.keyframes = {
            Keyframe{.offset = Fraction{1, 2}, .fret = {}, .bend = 1.0, .vibrato = {}},
            Keyframe{.offset = Fraction{4}, .fret = 7, .bend = {}, .vibrato = {}},
        };
        CHECK_FALSE(clipPayloadsToSustain(note, Fraction{1}));
        REQUIRE(note.keyframes.size() == 2);
        CHECK(note.keyframes[1].offset == Fraction{1});
        CHECK(note.keyframes[1].fret == 7);
    }
    SECTION("an interior point past the new end is erased, and that is a loss")
    {
        note.keyframes = {
            Keyframe{.offset = Fraction{3, 2}, .fret = {}, .bend = 1.0, .vibrato = {}},
            Keyframe{.offset = Fraction{4}, .fret = {}, .bend = 0.0, .vibrato = {}},
        };
        CHECK(clipPayloadsToSustain(note, Fraction{1}));
        // The end statement still rides back; only the bend point past the end is gone.
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{1});
    }
    SECTION("a ridden end statement overwriting a standing value is a loss")
    {
        note.keyframes = {
            Keyframe{.offset = Fraction{3}, .fret = 9, .bend = {}, .vibrato = {}},
            Keyframe{.offset = Fraction{4}, .fret = 3, .bend = {}, .vibrato = {}},
        };
        CHECK(clipPayloadsToSustain(note, Fraction{3}));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{3});
        // The 9 the point stated is gone under the carried slide-out.
        CHECK(note.keyframes[0].fret == 3);
    }
    SECTION("a ridden end statement restating the standing value loses nothing")
    {
        note.keyframes = {
            Keyframe{.offset = Fraction{3}, .fret = 3, .bend = {}, .vibrato = {}},
            Keyframe{.offset = Fraction{4}, .fret = 3, .bend = {}, .vibrato = {}},
        };
        CHECK_FALSE(clipPayloadsToSustain(note, Fraction{3}));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{3});
        CHECK(note.keyframes[0].fret == 3);
    }
    SECTION("an origin junction past the new end is erased, and the slide-out rides regardless")
    {
        // The slide-out toward 5 travels from the junction at 7. The cut erases that junction and
        // the slide-out still rides back, now toward the fret the onset already holds.
        note.keyframes = {
            Keyframe{.offset = Fraction{2}, .fret = 7, .bend = {}, .vibrato = {}},
            Keyframe{.offset = Fraction{4}, .fret = 5, .bend = {}, .vibrato = {}},
        };
        CHECK(clipPayloadsToSustain(note, Fraction{3, 2}));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{3, 2});
        CHECK(note.keyframes[0].fret == 5);
        CHECK(endStatedFretOrNull(note) != nullptr);
    }
    SECTION("vibrato stated exactly at the new end is shed, and that is a loss")
    {
        // The end is bare, so nothing rides: the point at 2 survives the inclusive bound and
        // becomes the end statement, which leaves no vibrato.
        note.keyframes = {
            Keyframe{.offset = Fraction{2}, .fret = 7, .bend = {}, .vibrato = VibratoState::Narrow},
        };
        CHECK(clipPayloadsToSustain(note, Fraction{2}));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{2});
        CHECK(note.keyframes[0].fret == 7);
        CHECK_FALSE(hasVibrato(note.keyframes[0].vibrato));
    }
    SECTION("a fret stated exactly at the new end loses nothing")
    {
        note.keyframes = {Keyframe{.offset = Fraction{2}, .fret = 7, .bend = {}, .vibrato = {}}};
        CHECK_FALSE(clipPayloadsToSustain(note, Fraction{2}));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{2});
        CHECK(note.keyframes[0].fret == 7);
    }
    SECTION("the end's writer landing on a standing vibrato sheds it, and that is a loss")
    {
        note.keyframes = {
            Keyframe{.offset = Fraction{4}, .fret = 7, .bend = {}, .vibrato = VibratoState::Narrow},
        };
        // The written fret equals the standing one, so the shed vibrato is the only loss.
        CHECK(setEndStatement(note, Keyframe{.offset = {}, .fret = 7, .bend = {}, .vibrato = {}}));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{4});
        CHECK(note.keyframes[0].fret == 7);
        CHECK_FALSE(hasVibrato(note.keyframes[0].vibrato));
    }
}

// THE OVERLAY LAW'S REPORT, which the clip's and the end writer's overwrite reports rest on: a
// channel both keyframes state with DIFFERENT values is an authored value gone, while an equal
// value, or a channel the standing keyframe never stated, loses nothing.
TEST_CASE(
    "The overlay reports a stated channel overwritten with a different value", "[core][chart]")
{
    const Keyframe standing{.offset = Fraction{1}, .fret = 9, .bend = 1.0, .vibrato = {}};

    Keyframe differing_fret = standing;
    CHECK(overlayKeyframe(
        differing_fret, Keyframe{.offset = {}, .fret = 3, .bend = {}, .vibrato = {}}));
    CHECK(differing_fret.fret == 3);

    Keyframe equal_fret = standing;
    CHECK_FALSE(
        overlayKeyframe(equal_fret, Keyframe{.offset = {}, .fret = 9, .bend = {}, .vibrato = {}}));
    CHECK(equal_fret.fret == 9);

    // The standing keyframe never stated vibrato, so stating one there overwrites nothing.
    Keyframe unstated_channel = standing;
    CHECK_FALSE(overlayKeyframe(
        unstated_channel,
        Keyframe{.offset = {}, .fret = {}, .bend = {}, .vibrato = VibratoState::Narrow}));
    CHECK(unstated_channel.vibrato == VibratoState::Narrow);

    Keyframe differing_bend = standing;
    CHECK(overlayKeyframe(
        differing_bend, Keyframe{.offset = {}, .fret = {}, .bend = 0.5, .vibrato = {}}));
    const std::optional<double>& pushed = differing_bend.bend;
    REQUIRE(pushed.has_value());
    if (pushed.has_value())
    {
        CHECK_THAT(*pushed, Catch::Matchers::WithinULP(0.5, 0));
    }
}

// The relational half of the technique matrix: E5/E12/E19 are resolver clauses rather than reasons
// to refuse a document, so each is a resolution instead of a refusal. Asked through
// chartResolutions rather than resolveLegato directly, because that is the path every consumer
// takes — the same-string walk that finds the predecessor and the span-extended hold table are
// under test with it.
TEST_CASE("Chart legato claims resolve against their predecessor", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    const auto make_note = [](const int beat, const int string, const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = beat};
        note.string = string;
        note.fret = fret;
        note.sustain = g_fixture_ring;
        return note;
    };
    // The claim is the LAST note; everything before it is its context.
    const auto resolve_claim = [&tempo_map](const std::vector<ChartNote>& notes) {
        REQUIRE_FALSE(notes.empty());
        const ChartConnections connections = chartConnections(notes, tempo_map);
        REQUIRE(connections.legato.size() == notes.size());
        return connections.legato.back();
    };
    const auto claim_at = [&make_note](const int beat, const int string, const int fret) {
        ChartNote note = make_note(beat, string, fret);
        note.attack = NoteAttack::Legato;
        return note;
    };

    SECTION("the fret at the ring's end picks the direction, and equal frets justify nothing")
    {
        // Predecessors hold their tails to the margin so the hold test never interferes with what
        // each case is about.
        ChartNote source = make_note(1, 1, 9);
        source.sustain = Fraction{1};
        CHECK(resolve_claim({source, claim_at(2, 1, 5)}) == LegatoMotion::Pull);

        ChartNote lower_source = make_note(1, 1, 3);
        lower_source.sustain = Fraction{1};
        CHECK(resolve_claim({lower_source, claim_at(2, 1, 5)}) == LegatoMotion::Hammer);

        ChartNote equal_source = make_note(1, 1, 5);
        equal_source.sustain = Fraction{1};
        CHECK(resolve_claim({equal_source, claim_at(2, 1, 5)}) == LegatoMotion::Unjustified);

        // A claim with nothing before it on its string resolves to nothing at all.
        CHECK(resolve_claim({claim_at(2, 1, 5)}) == LegatoMotion::Unjustified);

        // Same-STRING: a predecessor on another string is not a predecessor.
        ChartNote other_string = make_note(1, 2, 9);
        other_string.sustain = Fraction{1};
        CHECK(resolve_claim({other_string, claim_at(2, 1, 5)}) == LegatoMotion::Unjustified);
    }

    SECTION("the fret at the ring's end is where the finger ENDS")
    {
        // A 3->7 glide hands over 7, justifying a pull to 5 the onset frets alone would refuse.
        ChartNote gliding_source = make_note(1, 1, 3);
        gliding_source.sustain = Fraction{1};
        gliding_source.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 7}};
        CHECK(resolve_claim({gliding_source, claim_at(2, 1, 5)}) == LegatoMotion::Pull);

        // A scrape's travel is the PICK's position, not a finger's, so nothing waits at its end to
        // release or continue from: the note after a scrape is picked, whichever way the frets
        // fall. The hold reaches here, so the attack alone decides.
        ChartNote scrape_source = make_note(1, 1, 12);
        scrape_source.sustain = Fraction{1};
        scrape_source.attack = NoteAttack::PickSlide;
        setSlideOut(scrape_source, 7);
        CHECK(resolve_claim({scrape_source, claim_at(2, 1, 5)}) == LegatoMotion::Unjustified);
        CHECK(resolve_claim({scrape_source, claim_at(2, 1, 9)}) == LegatoMotion::Unjustified);
    }

    SECTION("a pull-off can neither sound nor release a harmonic")
    {
        ChartNote source = make_note(1, 1, 9);
        source.sustain = Fraction{1};

        // A pull-off releases onto a plain stopped pitch ringing the full speaking length, so a
        // claim carrying a node has no pull to resolve to.
        ChartNote harmonic_claim = claim_at(2, 1, 5);
        harmonic_claim.harmonic_node = 17.0;
        CHECK(resolve_claim({source, harmonic_claim}) == LegatoMotion::Unjustified);

        // The veto belongs to the pull clause ALONE, which is what makes the asymmetry a rule
        // rather than a blanket "no nodes": the same node above its predecessor is the tapped
        // harmonic — the fretting hand strikes the stop while the picking hand damps the node —
        // and the hammer clause accepts a node as something to strike.
        ChartNote lower_source = make_note(1, 1, 3);
        lower_source.sustain = Fraction{1};
        ChartNote struck_harmonic = claim_at(2, 1, 9);
        struck_harmonic.harmonic_node = 21.0;
        CHECK(resolve_claim({lower_source, struck_harmonic}) == LegatoMotion::Hammer);

        // A fret-hand harmonic predecessor is a touch, not a press: it holds nothing to hand over.
        ChartNote harmonic_source = make_note(1, 1, 0);
        harmonic_source.harmonic_node = 12.0;
        harmonic_source.sustain = Fraction{1};
        CHECK(resolve_claim({harmonic_source, claim_at(2, 1, 5)}) == LegatoMotion::Unjustified);
    }

    SECTION("a dead predecessor is ordinary, and the hold test alone bounds the muted cluck")
    {
        // Its finger is on the stop, so the muted hammer or pull that follows is a connection
        // like any other — disqualifying it would turn every imported cluck into a picked note.
        // It is bounded by the same one test reading the same field: a dead note stores the
        // duration its damped stroke lasts — only the DRAWN tail goes with E25 — so a cluck
        // chained to its restrike connects in both directions.
        ChartNote dead_above = make_note(1, 1, 9);
        dead_above.dead = true;
        dead_above.sustain = Fraction{1, 2};
        ChartNote close_claim = claim_at(1, 1, 5);
        close_claim.position.offset = Fraction{1, 2};
        CHECK(resolve_claim({dead_above, close_claim}) == LegatoMotion::Pull);

        ChartNote dead_below = make_note(1, 1, 3);
        dead_below.dead = true;
        dead_below.sustain = Fraction{1, 2};
        CHECK(resolve_claim({dead_below, close_claim}) == LegatoMotion::Hammer);

        // A cluck the hand left long before is a released string, dead or not — a strike a beat
        // after a muted scratch is a fresh one, which is the left-hand tap's statement. The ring
        // is the rule; there is no dead-note exception in either direction.
        CHECK(resolve_claim({dead_above, claim_at(2, 1, 5)}) == LegatoMotion::Unjustified);
        ChartNote left_tap = make_note(2, 1, 5);
        left_tap.attack = NoteAttack::LeftTap;
        CHECK(resolve_claim({dead_above, left_tap}) == LegatoMotion::Hammer);

        // And a claim ON a dead note from a ringing predecessor — the muted hammer of E24.
        ChartNote source = make_note(1, 1, 3);
        source.sustain = Fraction{1};
        ChartNote dead_claim = claim_at(2, 1, 5);
        dead_claim.dead = true;
        CHECK(resolve_claim({source, dead_claim}) == LegatoMotion::Hammer);
    }

    SECTION("a claim needs its predecessor still ringing at the onset")
    {
        // Strict adjacency, and nothing else: the chart states how long the string sounds, so a
        // predecessor whose ring stops short of the onset is a released string with nothing left
        // to connect to. An eighth-of-a-beat ring a beat back is exactly that.
        ChartNote source = make_note(1, 1, 9);
        CHECK(resolve_claim({source, claim_at(2, 1, 5)}) == LegatoMotion::Unjustified);

        // Reaching the onset exactly is reaching — which is the longest ring the bound allows, so
        // every justified claim in a normalized chart is this case.
        source.sustain = Fraction{1};
        CHECK(resolve_claim({source, claim_at(2, 1, 5)}) == LegatoMotion::Pull);

        // Across a wider gap the ring must reach all the way, not merely exist: a ring one margin
        // short does not count, which is what makes claims after a rest flatten.
        ChartNote far_source = make_note(1, 1, 9);
        far_source.sustain = Fraction{11, 4};
        CHECK(resolve_claim({far_source, claim_at(4, 1, 5)}) == LegatoMotion::Unjustified);
        far_source.sustain = Fraction{3};
        CHECK(resolve_claim({far_source, claim_at(4, 1, 5)}) == LegatoMotion::Pull);

        // A short gap is no exception either: a chug connects because Guitar Pro tiles durations
        // and its ring genuinely reaches the restrike, not because the gap is small.
        ChartNote close_claim = claim_at(1, 1, 5);
        close_claim.position.offset = Fraction{1, 2};
        CHECK(resolve_claim({make_note(1, 1, 9), close_claim}) == LegatoMotion::Unjustified);
        ChartNote chug = make_note(1, 1, 9);
        chug.sustain = Fraction{1, 2};
        CHECK(resolve_claim({chug, close_claim}) == LegatoMotion::Pull);
    }

    SECTION("a hand-shape span implies a hold for the display, never for a claim")
    {
        // The span convention answers how long the HAND stays down (chartHolds), which is a
        // display length. What a claim reads is the stored ring, so a span cannot lend one.
        //
        // The covering span is not supplied — a two-string strum DERIVES one, which is what the
        // hold assertion below also proves.
        const ChartNote low = make_note(1, 1, 9);
        const ChartNote high = make_note(1, 2, 9);
        const ChartNote claim = claim_at(2, 1, 5);

        CHECK(resolve_claim({low, high, claim}) == LegatoMotion::Unjustified);

        // The member that actually rings to the onset justifies it, span or no span.
        ChartNote ringing_low = low;
        ringing_low.sustain = Fraction{1};
        CHECK(resolve_claim({ringing_low, high, claim}) == LegatoMotion::Pull);

        // And what the span says about the DISPLAY: the strum's members are held while the shape
        // is, capped at each one's own ring.
        const ChartResolutions resolutions = chartResolutions({low, high, claim}, tempo_map);
        REQUIRE(resolutions.shapes.size() == 1);
        CHECK(resolutions.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        REQUIRE(resolutions.holds.size() == 3);
        CHECK(resolutions.holds[0] == g_fixture_ring);
        CHECK(resolutions.holds[1] == g_fixture_ring);
    }

    SECTION("resolutions judge the SAVED form, so a scrape's latent mute changes nothing")
    {
        // E2 forbids a mute on any saved scrape, so a dead flag found on one is purely the
        // in-memory latent the attack toggle preserves, and every resolution reads the saved form
        // it has already been stripped from.
        ChartNote dead_low = make_note(1, 1, 9);
        dead_low.dead = true;
        // The strum's second fretting-hand member: it is what makes the onset a chord and derives
        // the covering span, so the group below is a real strum under real furniture and its zeros
        // are the choke rather than a missing span.
        ChartNote dead_high = make_note(1, 3, 9);
        dead_high.dead = true;
        ChartNote latent_scrape = make_note(1, 2, 0);
        latent_scrape.attack = NoteAttack::PickSlide;
        latent_scrape.dead = true;
        latent_scrape.sustain = Fraction{1};
        setSlideOut(latent_scrape, 7);
        CHECK_FALSE(savedChartNote(latent_scrape).dead);

        // Both forms answer the same way, and no unanimity rule is involved: a dead member is
        // choked ON ITS OWN ACCOUNT, so an entirely dead group chokes with nothing to flip, and a
        // scrape is a right-hand onset that no strum counts or holds either way. The latent mute
        // has no route into the table at all — which is the shape a stripped-before-read field
        // should have.
        const std::vector<ChartNote> in_memory{
            dead_low, latent_scrape, dead_high, claim_at(2, 1, 5)
        };
        const std::vector<ChartNote> as_saved{
            dead_low, savedChartNote(latent_scrape), dead_high, claim_at(2, 1, 5)
        };
        const ChartResolutions memory_resolutions = chartResolutions(in_memory, tempo_map);
        const ChartResolutions saved_resolutions = chartResolutions(as_saved, tempo_map);
        REQUIRE(memory_resolutions.holds.size() == 4);
        REQUIRE(saved_resolutions.holds.size() == 4);
        // Each dead member holds nothing: a percussive choke is not a grip, however far the shape
        // above it runs.
        CHECK(memory_resolutions.holds[0] == Fraction{});
        CHECK(memory_resolutions.holds[2] == Fraction{});
        // The scrape holds exactly what it draws: its one-beat gesture, spaced a margin before the
        // head a beat later like every other drawn tail — its terminal is the statement at its
        // ring's end, and rule 2 carries that with the drawn end. A number of its own is what says
        // the two zeros above are the mute rather than a span that failed to cover the onset.
        CHECK(memory_resolutions.holds[1] == Fraction{9, 10});
        CHECK(memory_resolutions.holds[0] == saved_resolutions.holds[0]);
        CHECK(memory_resolutions.holds[1] == saved_resolutions.holds[1]);
        CHECK(memory_resolutions.holds[2] == saved_resolutions.holds[2]);

        // The fret-hand harmonic predicate keeps the same discipline for a scrape's latent node:
        // the exclusion is about the ATTACK owning the node, not about having a slide-out.
        latent_scrape.harmonic_node = 12.0;
        CHECK_FALSE(fretHandHarmonic(latent_scrape));
        ChartNote natural = make_note(1, 1, 0);
        natural.harmonic_node = 12.0;
        CHECK(fretHandHarmonic(natural));
    }

    SECTION("a left-hand tap resolves to the hammer motion with no predecessor at all")
    {
        ChartNote left_tap = make_note(2, 1, 5);
        left_tap.attack = NoteAttack::LeftTap;
        CHECK(resolve_claim({left_tap}) == LegatoMotion::Hammer);

        // Not even a predecessor that would disprove a claim can withdraw it: the statement is
        // local. Here an equal fret at the ring's end justifies nothing, and the tap resolves
        // anyway.
        ChartNote equal_source = make_note(1, 1, 5);
        equal_source.sustain = Fraction{1};
        CHECK(resolve_claim({equal_source, left_tap}) == LegatoMotion::Hammer);
    }

    SECTION("a plain pick resolves to nothing, but the resolver still answers the hypothetical")
    {
        // The per-chart table is what display reads, so a note that makes no claim must carry no
        // motion however justifiable a claim there would be — otherwise a plain pick after a higher
        // note would draw a pull-off mark.
        ChartNote source = make_note(1, 1, 9);
        source.sustain = Fraction{1};
        const ChartNote plain = make_note(2, 1, 5);
        CHECK(resolve_claim({source, plain}) == LegatoMotion::Unjustified);

        // Asked directly, the resolver answers what a claim WOULD resolve to — the question the `H`
        // toggle's plan is built from, and why it deliberately ignores the note's own attack.
        CHECK(resolveLegato(plain, &source, tempo_map) == LegatoMotion::Pull);
    }

    SECTION("resolution never cascades: a broken claim still justifies the note after it")
    {
        // Three notes down one string, the middle one making a claim nothing justifies. What the
        // last note connects to is the middle note's STORED fret at the ring's end and ring, never
        // the middle note's own answer — so a broken claim is not contagious, which is what lets
        // the sweep flatten in a single pass and lets display read the table with no fixed point to
        // reach.
        const ChartNote released = make_note(1, 1, 9);
        ChartNote broken = claim_at(2, 1, 5);
        broken.sustain = Fraction{1};
        const std::vector<ChartNote> notes{released, broken, claim_at(3, 1, 3)};

        const ChartConnections connections = chartConnections(notes, tempo_map);
        REQUIRE(connections.legato.size() == 3);
        // The bare predecessor at the bound is a proven release, so the middle claim resolves to
        // nothing — and the last note pulls off it regardless.
        CHECK(connections.legato[1] == LegatoMotion::Unjustified);
        CHECK(connections.legato[2] == LegatoMotion::Pull);

        // Which is exactly why the sweep needs no second pass: flattening the middle claim changes
        // nothing the last note's justification reads.
        std::vector<ChartNote> swept = notes;
        CHECK(sweepUnjustifiedLegato(swept, tempo_map).size() == 1);
        CHECK(swept[1].attack == NoteAttack::Pick);
        CHECK(swept[2].attack == NoteAttack::Legato);
        CHECK(sweepUnjustifiedLegato(swept, tempo_map).empty());
    }
}

// The two places an unjustifiable claim is flattened: the settle sweep every commit point runs, and
// the document writer, which runs it so no file can carry a claim the chart does not justify.
TEST_CASE("Chart settles unjustifiable legato claims", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    const auto note_at = [](const int beat, const int string, const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = beat};
        note.string = string;
        note.fret = fret;
        note.sustain = g_fixture_ring;
        return note;
    };
    // String 1 carries a justified claim (its predecessor rings exactly to the claim's onset half
    // a beat later); string 2 carries one with nothing to connect to; and a left-hand tap sits on
    // string 3 with no predecessor of its own.
    ChartNote ringing = note_at(1, 1, 5);
    ringing.sustain = Fraction{1, 2};
    ChartNote justified = note_at(1, 1, 7);
    justified.position.offset = Fraction{1, 2};
    justified.attack = NoteAttack::Legato;
    ChartNote unjustifiable = note_at(2, 2, 5);
    unjustifiable.attack = NoteAttack::Legato;
    ChartNote left_tap = note_at(3, 3, 9);
    left_tap.attack = NoteAttack::LeftTap;
    chart.notes = {ringing, justified, unjustifiable, left_tap};

    // Every chart the sweep touches is legal data before and after: the flatten is a resolution,
    // not a repair of something invalid.
    CHECK(validateChartRules(chart, tempo_map).has_value());

    SECTION("the sweep flattens only what nothing justifies, and says what it changed")
    {
        std::vector<ChartNote> notes = chart.notes;
        const std::vector<ChartConversion> conversions = sweepUnjustifiedLegato(notes, tempo_map);
        REQUIRE(conversions.size() == 1);
        // The conversion names the claim it flattened, so a load or an import can report which
        // one — typed by rule, with the place as text a charter can find.
        CHECK(conversions.front().repair == ChartRepair::UnjustifiedLegato);
        CHECK(conversions.front().where == "1:2 string 2");
        CHECK(notes[1].attack == NoteAttack::Legato);
        CHECK(notes[2].attack == NoteAttack::Pick);
        CHECK(notes[3].attack == NoteAttack::LeftTap);

        // Stateless and idempotent: a second pass over a settled stream finds nothing.
        CHECK(sweepUnjustifiedLegato(notes, tempo_map).empty());
    }

    SECTION("the writer serializes the resolved form, so no file carries a broken claim")
    {
        const auto written = parseChartDocument(chartDocumentText(chart, tempo_map));
        REQUIRE(written.has_value());
        REQUIRE(written->notes.size() == chart.notes.size());
        CHECK(written->notes[1].attack == NoteAttack::Legato);
        CHECK(written->notes[2].attack == NoteAttack::Pick);
        CHECK(written->notes[3].attack == NoteAttack::LeftTap);

        // Memory is richer than the file: the claim itself is untouched, so a later edit that
        // justifies it brings the connection back with no re-authoring.
        CHECK(chart.notes[2].attack == NoteAttack::Legato);
    }

    SECTION("the direction tokens are unknown, and the intent tokens round-trip")
    {
        const auto parse_attack = [](const std::string& token) {
            return parseChartDocument(
                R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
                R"( "notes": [ { "position": "1:1", "string": 1, "fret": 5, "sustain": "1/8",)"
                R"( "attack": ")" +
                token + R"(" } ] })");
        };
        CHECK_FALSE(parse_attack("hammer").has_value());
        CHECK_FALSE(parse_attack("pull").has_value());
        REQUIRE(parse_attack("legato").has_value());
        REQUIRE(parse_attack("leftTap").has_value());
        CHECK(parse_attack("legato")->notes[0].attack == NoteAttack::Legato);
        CHECK(parse_attack("leftTap")->notes[0].attack == NoteAttack::LeftTap);
    }
}

// Pick-slide notes: no pitched techniques in a saved document (the writer omits the in-memory
// overrides; accent is a scrape's own technique), the required unpitched slide-out terminal
// exactly at the sustain, and an always-traveling path.
TEST_CASE("Chart rules validate pick-slide notes", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // The chained scrape in the full fixture: fret 17, one turnaround keyframe, slide-out.
    constexpr std::size_t scrape = 7;

    const auto expect_invalid = [&tempo_map](const Chart& chart) {
        const auto result = validateChartRules(chart, tempo_map);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ChartErrorCode::InvalidPickSlide);
    };
    // A scrape that sits still is not refused as a scrape but repaired into the plain pick it
    // sounds like (the normalizer's demotion), so the refusal is the per-note fixpoint's.
    const auto expect_stilled = [&tempo_map](const Chart& chart) {
        const auto result = validateChartRules(chart, tempo_map);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ChartErrorCode::InvalidNote);
        CHECK(result.error().message.find("no longer travels") != std::string::npos);
    };

    Chart carried_technique = makeFullChart();
    carried_technique.notes[scrape].tremolo = true;
    expect_invalid(carried_technique);

    // Both mute flags are refused, not just the deadening: a scrape carries neither.
    Chart carried_dead = makeFullChart();
    carried_dead.notes[scrape].dead = true;
    expect_invalid(carried_dead);

    Chart carried_palm = makeFullChart();
    carried_palm.notes[scrape].palm_mute = true;
    expect_invalid(carried_palm);

    // An accented scrape is legal — an aggressively played pick slide, the scrape's own
    // technique rather than an override.
    Chart accented = makeFullChart();
    accented.notes[scrape].emphasis = NoteEmphasis::Accent;
    CHECK(validateChartRules(accented, tempo_map).has_value());

    // A ghosted scrape is legal for the same reason, at the other end of the axis: a lightly
    // played one. Emphasis is dynamics, so it composes with every attack there is.
    Chart ghosted = makeFullChart();
    ghosted.notes[scrape].emphasis = NoteEmphasis::Ghost;
    CHECK(validateChartRules(ghosted, tempo_map).has_value());

    Chart missing_terminal = makeFullChart();
    clearSlideOut(missing_terminal.notes[scrape]);
    expect_invalid(missing_terminal);

    // Turnaround keyframes are optional: a plain start-to-terminal scrape is the common case. The
    // terminal is not one of them — it is the slide-out the path must still end on — so the clear
    // below states it again rather than leaving the gesture without one.
    Chart no_turnarounds = makeFullChart();
    no_turnarounds.notes[scrape].keyframes.clear();
    setSlideOut(no_turnarounds.notes[scrape], 9);
    CHECK(validateChartRules(no_turnarounds, tempo_map).has_value());

    // A ring longer than the notated gesture is not a shape at all: the terminal IS the keyframe
    // at the ring's end (W11), so a scrape's terminal rides a resize in BOTH directions and a ring
    // outrunning its gesture cannot be written down. Resized through the one way a ring changes
    // length, which is what carries the terminal along.
    Chart longer_ring = makeFullChart();
    static_cast<void>(clipPayloadsToSustain(longer_ring.notes[scrape], Fraction{3, 2}));
    CHECK(longer_ring.notes[scrape].sustain == Fraction{3, 2});
    CHECK(validateChartRules(longer_ring, tempo_map).has_value());

    // The travel is the gesture: a path leg that starts where it ends has nothing to scrape,
    // unlike note slides, whose equal-fret segments are legitimate holds.
    Chart stationary_start = makeFullChart();
    stationary_start.notes[scrape].keyframes[0].fret = 17;
    expect_stilled(stationary_start);

    Chart stationary_terminal = makeFullChart();
    setSlideOut(stationary_terminal.notes[scrape], 5);
    expect_stilled(stationary_terminal);

    // A scrape's terminal may stand exactly on the next head of its string, like any other end
    // statement: the store holds where the pick actually stopped, and presentation alone spaces the
    // chip before the head. So nothing here is refused and nothing is moved.
    Chart terminal_on_onset;
    terminal_on_onset.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    terminal_on_onset.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 12,
            .sustain = Fraction{1, 2},
            .attack = NoteAttack::PickSlide,
            .bend = 0.0,
            .keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 4}},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_ring,
            .bend = 0.0,
            .keyframes = {},
        },
    };
    // The ring is already inside its bound, so the load path finds nothing at all to repair and the
    // terminal stays exactly on the head the charter ran it to.
    CHECK(validateChartRules(terminal_on_onset, tempo_map).has_value());
    CHECK(normalizeChart(terminal_on_onset, tempo_map).empty());
    CHECK(terminal_on_onset.notes[0].sustain == Fraction{1, 2});
    REQUIRE(terminal_on_onset.notes[0].keyframes.size() == 1);
    CHECK(terminal_on_onset.notes[0].keyframes.front().offset == Fraction{1, 2});
    CHECK(endStatedFretOrNull(terminal_on_onset.notes[0]) != nullptr);

    // An interior stop on the head with the ring running PAST it: the truncation cuts to exact
    // adjacency and makes that stop the terminal (a fret at the ring's end is the slide-out), on
    // the head, with nothing else to report.
    Chart interior_on_onset = terminal_on_onset;
    interior_on_onset.notes[0].sustain = Fraction{1};
    interior_on_onset.notes[0].keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 4}};
    setSlideOut(interior_on_onset.notes[0], 9);
    const std::vector<ChartConversion> interior_moved =
        normalizeChart(interior_on_onset, tempo_map);
    REQUIRE(interior_moved.size() == 1);
    CHECK(interior_moved.front().repair == ChartRepair::OverlappingTail);
    CHECK(interior_on_onset.notes[0].sustain == Fraction{1, 2});
    CHECK(validateChartRules(interior_on_onset, tempo_map).has_value());
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
    scrape.vibrato = VibratoState::Narrow;
    scrape.palm_mute = true;
    scrape.dead = true;
    scrape.emphasis = NoteEmphasis::Accent;

    const auto parsed = parseChartDocument(chartDocumentText(chart, makeTempoMap()));
    REQUIRE(parsed.has_value());
    const ChartNote& saved = parsed->notes[7];
    CHECK(saved.attack == NoteAttack::PickSlide);
    CHECK_FALSE(saved.tremolo);
    CHECK_FALSE(hasVibrato(saved.vibrato));
    CHECK_FALSE(saved.palm_mute);
    CHECK_FALSE(saved.dead);
    CHECK(saved.emphasis == NoteEmphasis::Accent);
    const int* const saved_slide_out = endStatedFretOrNull(saved);
    REQUIRE(saved_slide_out != nullptr);
    if (saved_slide_out != nullptr)
    {
        CHECK(*saved_slide_out == 9);
    }
    CHECK(validateChartRules(*parsed, makeTempoMap()).has_value());
}

// The shared arrival rule: a strummed chord is a box; a posture string carried into the span start
// without an onset there, a partial sounding inside it, or a right-hand onset over it renders
// arpeggio-style. A string whose ring ENDED before the strum keeps the box: nothing was carried.
TEST_CASE("Chart shape arrival classifies boxes and arpeggios", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    constexpr GridPosition strum_at{.measure = 2, .beat = 2};

    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // A sustained note on string 2 rings from 2:1 through 2:3; a two-string strum lands at 2:2 and
    // rings a beat, which is the span the cases below classify.
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 6,
            .sustain = Fraction{2},
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = strum_at,
            .string = 1,
            .fret = 3,
            .sustain = Fraction{1},
            .bend = 0.0,
            .keyframes = {},
        },
        ChartNote{
            .position = strum_at,
            .string = 3,
            .fret = 8,
            .sustain = Fraction{1},
            .bend = 0.0,
            .keyframes = {},
        },
    };

    // String 2's fret 6 is carried into the strum, un-restruck and still ringing: the strum picks
    // around it, so two of the shape's three members sound there and the span is an arpeggio.
    CHECK(arrivesAsArpeggio(chart.notes, strum_at, tempo_map));

    // One span, and it DATES from the ringing note rather than from the strum (THE ACCUMULATION
    // LAW): the ring and the strum's members overlap into one shape, and the front is the earliest
    // of their onsets that no preceding span covers. The strum arrives inside the statement rather
    // than opening it, which is what makes "fewer than two sounds at a span start" a precondition
    // rather than a trigger.
    const ChartResolutions resolved = chartResolutions(chart.notes, tempo_map);
    REQUIRE(resolved.shapes.size() == 1);
    CHECK(resolved.shapes.front().position == GridPosition{.measure = 2, .beat = 1});

    // With the ring ended before the strum, nothing is carried: the shape is the two struck
    // strings, both sound at its start, and the chord box stands.
    chart.notes[0].sustain = Fraction{1, 2};
    CHECK_FALSE(arrivesAsArpeggio(chart.notes, strum_at, tempo_map));

    // A tapped note sounding within the span turns that box into a held arpeggio: the fretting
    // hand holds the shape while the right hand taps above it (held-chord-under-tap).
    Chart tapped_over_hold = chart; // the box state above (the ring ended before the strum)
    tapped_over_hold.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 15,
            .sustain = g_fixture_ring,
            .attack = NoteAttack::Tap,
            .bend = 0.0,
            .keyframes = {},
        });
    CHECK(arrivesAsArpeggio(tapped_over_hold.notes, strum_at, tempo_map));

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
            .bend = 0.0,
            .keyframes = {Keyframe{.offset = Fraction{1, 4}, .fret = 3}},
        });
    CHECK(arrivesAsArpeggio(scraped_over_hold.notes, strum_at, tempo_map));

    // A tap OUTSIDE the span (after it ends) leaves the box a box.
    Chart tapped_after = chart;
    tapped_after.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 3, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 15,
            .sustain = g_fixture_ring,
            .attack = NoteAttack::Tap,
            .bend = 0.0,
            .keyframes = {},
        });
    CHECK_FALSE(arrivesAsArpeggio(tapped_after.notes, strum_at, tempo_map));

    // A RIGHT-HAND ring crossing the start carries nothing: a tap joins no posture, so the strum
    // states its own two strings whole and stays a box. A fretting-hand ring across a start is
    // always folded IN, so the only ring that can cross one and leave the shape alone is the other
    // hand's.
    chart.notes[0].sustain = Fraction{2};
    Chart tapped_before = chart;
    tapped_before.notes[0].attack = NoteAttack::Tap;
    CHECK_FALSE(arrivesAsArpeggio(tapped_before.notes, strum_at, tempo_map));

    // A ring from an earlier SPAN is no member: giving string 2 a co-struck partner at 2:1 makes
    // the pair a span of their own, so by A RING BELONGS ONLY TO THE SPAN IT WAS STRUCK IN the
    // ring crossing the strum belongs there and nothing is carried into it. The strum states its
    // own two strings whole and is a box — the same answer the un-carried case above gives, now
    // reached because the ring was spoken for rather than because it had stopped.
    Chart chord_sourced_ring = chart;
    chord_sourced_ring.notes.insert(
        chord_sourced_ring.notes.begin() + 1,
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 4,
            .fret = 5,
            .sustain = g_fixture_ring,
            .bend = 0.0,
            .keyframes = {},
        });
    CHECK_FALSE(arrivesAsArpeggio(chord_sourced_ring.notes, strum_at, tempo_map));

    // A DEAD string's carry classifies. The class is a fact about the HANDS — the finger is
    // still down and the strum still picks around it — so it reads the STORED ring, the same one
    // the walk's fold-in reads. E25 takes the tail off what a surface DRAWS and says nothing about
    // what the hands were doing; letting it decide here would flip the span at an interior slot
    // and not at its start.
    Chart dead_ring = chart;
    dead_ring.notes[0].dead = true;
    CHECK(arrivesAsArpeggio(dead_ring.notes, strum_at, tempo_map));
}

// A `held` KEY IS IGNORED: a held stop is derived, never stored, so a document carrying the key
// reads exactly as though it did not — whatever the value's type — and the writer never emits it.
TEST_CASE("A chart document ignores the retired held key", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const auto parsed = parseChartDocument(
        R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
        R"( "notes": [ { "position": "1:1", "string": 1, "fret": 12, "sustain": "1/4",)"
        R"( "attack": "tap", "held": "5" } ] })");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->notes.size() == 1);
    CHECK(parsed->notes.front().fret == 12);
    CHECK(parsed->notes.front().attack == NoteAttack::Tap);
    CHECK(chartDocumentText(*parsed, tempo_map).find("held") == std::string::npos);
}

// The record a tap harmonic IS: the fretting hand presses a stop and the tapping finger touches a
// node above it, so one note states the stop the string speaks from and the point the touch lies
// at — the same two facts an artificial harmonic states, in the same two fields. Three layers have
// to agree about it — the rules, the derivation and the document — and every one of them reads the
// pressed stop out of `fret` (\ref physicalStopFret).
//
// The form is DISABLED for now: the rules refuse it by name, so the validation sections below pin
// the refusal, while the derivation and document sections keep pinning the settled record and the
// display it earns — the code that draws it stays in place behind the one refusing rule.
TEST_CASE("A tapped harmonic states its pressed stop and its node", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    // Press fret 5 and tap the octave node twelve frets above it — the commonest tapped harmonic
    // there is: the pressed fret never sounds directly, it sounds as the fundamental this overtone
    // divides.
    const auto tapped_harmonic = [] {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 3;
        note.fret = 5;
        note.sustain = Fraction{1, 2};
        note.attack = NoteAttack::Tap;
        note.harmonic_node = 17.0;
        return note;
    };
    // A second member at the same slot, so rule 10 opens a span at all: the hand is holding a shape
    // and the tap is played over one of its stops.
    const auto struck_member = [] {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 1;
        note.fret = 7;
        note.sustain = Fraction{1, 2};
        return note;
    };
    const auto make_chart = [&](std::vector<ChartNote> notes) {
        Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = std::move(notes);
        std::ranges::sort(chart.notes, chartNoteOrderLess);
        return chart;
    };
    const Chart chart = make_chart({struck_member(), tapped_harmonic()});

    SECTION("the form is refused for now, after the node is judged against the note's own fret")
    {
        // Tapped and artificial harmonics are DISABLED: the rules refuse the record outright so no
        // chart can hold one until the forms are reopened. The refusal names the cause.
        const auto disabled = validateChartRules(chart, tempo_map);
        REQUIRE_FALSE(disabled.has_value());
        CHECK(disabled.error().message.find("not supported yet") != std::string::npos);

        // The node rule still binds FIRST — a node lies on the speaking length, so it cannot sit at
        // or behind the stop — and the stop it binds against is this note's fret. A node level with
        // the pressed 5 is refused for that reason before the disable rule is reached, which keeps
        // the two refusals a discrimination rather than one blanket answer.
        ChartNote behind = tapped_harmonic();
        behind.harmonic_node = 5.0;
        const auto refused = validateChartRules(make_chart({behind}), tempo_map);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message.find("beyond the stop") != std::string::npos);

        ChartNote inside = tapped_harmonic();
        inside.harmonic_node = 10.0;
        const auto inside_refused = validateChartRules(make_chart({inside}), tempo_map);
        REQUIRE_FALSE(inside_refused.has_value());
        CHECK(inside_refused.error().message.find("not supported yet") != std::string::npos);

        // And the OPEN string's stop is the capo, which is the other half of the same reading: a
        // natural harmonic states fret 0, so what the node is measured from is whatever stops the
        // string there. Under a capo at 3 a node at 3 sits AT the stop and is refused, while the
        // twelfth-fret node of the capo'd string, fifteen frets along the board, is legal.
        const auto capoed = [&tempo_map](const double node) {
            ChartNote natural;
            natural.position = GridPosition{.measure = 1, .beat = 1};
            natural.string = 3;
            natural.sustain = Fraction{1, 2};
            natural.harmonic_node = node;
            Chart chart_with_capo;
            chart_with_capo.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
            chart_with_capo.tuning.capo = 3;
            chart_with_capo.notes = {natural};
            return validateChartRules(chart_with_capo, tempo_map);
        };
        const auto at_the_capo = capoed(3.0);
        REQUIRE_FALSE(at_the_capo.has_value());
        CHECK(at_the_capo.error().message.find("beyond the stop") != std::string::npos);
        CHECK(capoed(15.0).has_value());
    }

    SECTION("the document carries both")
    {
        const std::string text = chartDocumentText(chart, tempo_map);
        CHECK(text.find(R"("fret": 5)") != std::string::npos);
        CHECK(text.find(R"("harmonicNode": 17)") != std::string::npos);
        CHECK(text.find(R"("held")") == std::string::npos);
        const auto parsed = parseChartDocument(text);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->notes.size() == chart.notes.size());
        CHECK(parsed->notes == chart.notes);
    }
}

// AND THE DERIVATION IS BOUND BY THE RELEASE ALONE: a pull-off proves a finger on its landing stop
// at the release, whatever path the source travelled first, so a stop the source's own path swept
// derives exactly as one it never touched. (The traveled range bounds the ride, not the
// derivation.)
TEST_CASE("A pull-off states its stop whatever the onset's own travel", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // A tap and the note that pulls off it onto fret 5, differing in ONE thing: whether the tap's
    // own path passes through 5. The FRET AT THE RING'S END is 12 either way, so both resolve the
    // same pull and both offer the same candidate stop.
    const auto figure = [&tempo_map](const std::optional<int> travels_from) {
        Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        ChartNote tap;
        tap.position = GridPosition{.measure = 1, .beat = 1};
        tap.string = 1;
        tap.fret = travels_from.value_or(12);
        tap.sustain = Fraction{1};
        tap.attack = NoteAttack::Tap;
        if (travels_from.has_value())
        {
            tap.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 12}};
        }
        ChartNote successor;
        successor.position = GridPosition{.measure = 1, .beat = 2};
        successor.string = 1;
        successor.fret = 5;
        successor.sustain = Fraction{1};
        successor.attack = NoteAttack::Legato;
        chart.notes = {tap, successor};
        REQUIRE(validateChartRules(chart, tempo_map).has_value());
        return chart;
    };

    SECTION("a tap keyframed up past the fret still states it")
    {
        // The hull runs 3 through 12 and 5 sits inside it: the finger arrived behind the tapping
        // slide and was waiting on 5 when it lifted.
        const Chart chart = figure(3);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartPlantedStops(connections).front() == std::optional{5});
    }

    SECTION("the untravelled tap still states it")
    {
        // The discrimination: one keyframe apart, and the hull collapses to the tap's own point.
        const Chart chart = figure(std::nullopt);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartPlantedStops(connections).front() == std::optional{5});
    }
}

// A SLID fretting-hand source plants its stop too — the two-finger landing: 5 slides to 9, a finger
// arrives behind it on 7, and the pull-off lands there. The span gains nothing from it: the path
// swept 7, so the source cannot ride it.
TEST_CASE("A slid pull-off source plants its stop", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    ChartNote source;
    source.position = GridPosition{.measure = 1, .beat = 1};
    source.string = 1;
    source.fret = 5;
    source.sustain = Fraction{1};
    source.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 9}};
    ChartNote successor;
    successor.position = GridPosition{.measure = 1, .beat = 2};
    successor.string = 1;
    successor.fret = 7;
    successor.sustain = Fraction{1};
    successor.attack = NoteAttack::Legato;
    chart.notes = {source, successor};
    REQUIRE(validateChartRules(chart, tempo_map).has_value());

    const ChartConnections connections = chartConnections(chart.notes, tempo_map);
    REQUIRE(connections.legato[1] == LegatoMotion::Pull);
    CHECK(chartPlantedStops(connections).front() == std::optional{7});
    // The source's path swept 7, so it cannot ride a grip holding 7.
    CHECK_FALSE(gripStatement(chart.notes.front(), std::optional{7}, frettedStop(7)).has_value());

    Arrangement arrangement;
    arrangement.chart = chart;
    const ChartViewState view = makeChartViewState(arrangement, tempo_map);
    REQUIRE(view.notes.size() == 2);
    CHECK(view.notes.front().held_fret == std::optional{7});
}

// The same law under a TAP: a tap that slides 12 to 16 and is pulled off onto 9 holds that 9, the
// finger waiting where the tapping finger lets go.
TEST_CASE("A slid tap holds its plant", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    ChartNote tap;
    tap.position = GridPosition{.measure = 1, .beat = 1};
    tap.string = 1;
    tap.fret = 12;
    tap.sustain = Fraction{1};
    tap.attack = NoteAttack::Tap;
    tap.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 16}};
    ChartNote successor;
    successor.position = GridPosition{.measure = 1, .beat = 2};
    successor.string = 1;
    successor.fret = 9;
    successor.sustain = Fraction{1};
    successor.attack = NoteAttack::Legato;
    chart.notes = {tap, successor};
    REQUIRE(validateChartRules(chart, tempo_map).has_value());

    Arrangement arrangement;
    arrangement.chart = chart;
    const ChartViewState view = makeChartViewState(arrangement, tempo_map);
    REQUIRE(view.notes.size() == 2);
    CHECK(view.notes.front().held_fret == std::optional{9});
}

// THE HOLD-UNDER LAW's derivation half: the planted stop is a fact about EVERY pull-off source,
// whichever hand made its onset.
TEST_CASE("A pull-off plants its stop under a fretting-hand source too", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // One figure, varying only the source's attack and the destination's fret: a source at 7 on
    // string 1, and a same-string successor claiming legato one beat later.
    const auto figure = [&tempo_map](const NoteAttack source_attack, const int destination_fret) {
        Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        ChartNote source;
        source.position = GridPosition{.measure = 1, .beat = 1};
        source.string = 1;
        source.fret = 7;
        source.sustain = Fraction{1};
        source.attack = source_attack;
        ChartNote successor;
        successor.position = GridPosition{.measure = 1, .beat = 2};
        successor.string = 1;
        successor.fret = destination_fret;
        successor.sustain = Fraction{1};
        successor.attack = NoteAttack::Legato;
        chart.notes = {source, successor};
        REQUIRE(validateChartRules(chart, tempo_map).has_value());
        return chart;
    };

    SECTION("a fretting-hand source plants")
    {
        const Chart chart = figure(NoteAttack::Pick, 5);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartPlantedStops(connections).front() == std::optional{5});
    }

    SECTION("a tap source plants alike")
    {
        const Chart chart = figure(NoteAttack::Tap, 5);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartPlantedStops(connections).front() == std::optional{5});
    }

    SECTION("a hammer plants nothing")
    {
        const Chart chart = figure(NoteAttack::Pick, 9);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Hammer);
        CHECK_FALSE(chartPlantedStops(connections).front().has_value());
    }

    SECTION("a pull onto an open string plants the open string")
    {
        // Every fret derives alike, zero included: the stop the pull-off states beneath its source
        // is the one the string falls to, and the open string is that stop — always waiting, no
        // finger needed. Only an undefined destination derives nothing.
        const Chart chart = figure(NoteAttack::Pick, 0);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartPlantedStops(connections).front() == std::optional{0});
    }

    SECTION("a natural harmonic never plants: the resolver refuses it as a source")
    {
        // A planted grip is always a PRESSED one, which is what lets the span machine lift every
        // plant through frettedStop: the resolver refuses a fret-hand harmonic as a legato source,
        // so a node is never on either end of the pull-off relation.
        Chart chart = figure(NoteAttack::Pick, 5);
        chart.notes.front().fret = 0;
        chart.notes.front().harmonic_node = 12.0;
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        CHECK(connections.legato[1] == LegatoMotion::Unjustified);
        CHECK_FALSE(chartPlantedStops(connections).front().has_value());
    }
}

} // namespace rock_hero::common::core
