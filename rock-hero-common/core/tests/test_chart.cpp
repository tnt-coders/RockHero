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
                 // The release: a fret stated exactly at the ring's end, where the hand leaves
                 // toward it rather than sounding it.
                 Keyframe{.offset = Fraction{1, 2}, .fret = 15}},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{4},
            .vibrato = VibratoState::Narrow,
            // The bend channel: an unbent onset, then a curl, a full step, and a release, each on
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
            // margin INSIDE the ring — at the end it would be the release instead — and the ring
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
        // optional turnaround keyframes and the required unpitched terminal as the RELEASE, the
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
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 7, .width = 5},
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
    const std::vector<bool> arrivals =
        chartShapeArrivals(resolved.presented_notes, resolved.shapes, tempo_map);
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
        // The texture half is a same-length row of nothing — the shape the derivation publishes,
        // one entry per string on both halves — so the grip alone is what differs across these
        // three; the two halves are disjoint by construction, and this section is about the grip's
        // rows.
        const ChartPosture node_grip{
            .stops = {nodeStop(12.0), std::nullopt, nodeStop(12.0)},
            .texture = {std::nullopt, std::nullopt, std::nullopt}
        };
        const ChartPosture same_grip{
            .stops = {nodeStop(12.0), std::nullopt, nodeStop(12.0)},
            .texture = {std::nullopt, std::nullopt, std::nullopt}
        };
        const ChartPosture fret_grip{
            .stops = {frettedStop(12), std::nullopt, frettedStop(12)},
            .texture = {std::nullopt, std::nullopt, std::nullopt}
        };
        CHECK(node_grip == same_grip);
        CHECK(node_grip != fret_grip);
        // And the OTHER half discriminates too, which is what the derivation's dedup rests on: the
        // same grip printing a different texture is a different posture, so a span that picks up a
        // ringing open string never folds onto the row it had before that string sounded.
        const ChartPosture rung_under{
            .stops = {nodeStop(12.0), std::nullopt, nodeStop(12.0)},
            .texture = {std::nullopt, frettedStop(0), std::nullopt}
        };
        CHECK(rung_under != node_grip);

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
// two part company at the pinch (RULED 2026-09-17).
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
// as the floor line's run from the stop to the node (RULED 2026-09-18).
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
// from the notes now (deriveChartShapes), so a document carrying either key states a second,
// unverifiable copy of what the notes already say. Silently ignoring them is the failure this
// pins — every un-reimported package would load with its stored picture discarded and no word
// said. Delete this with the tripwire once the corpus is re-imported.
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
// like a default — a bend released back to zero and a vibrato that ENDS both state values a
// shorter spelling would silently delete.
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
        Keyframe{.offset = Fraction{1}, .vibrato = VibratoState::Off},
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

    // The two default-looking statements survive as STATEMENTS. Round-trip equality alone would
    // still pass if the writer dropped them and the reader defaulted them back, so each is also
    // asserted as present and its unstated neighbours as absent.
    const std::vector<Keyframe>& keyframes = parsed->notes[0].keyframes;
    REQUIRE(keyframes.size() == 4);
    CHECK(keyframes[0].fret.has_value());
    CHECK_FALSE(keyframes[0].bend.has_value());
    CHECK_FALSE(keyframes[0].vibrato.has_value());
    // Each optional is bound once and guarded by that name: the checker cannot tie two separate
    // reads of an indexed element together.
    const std::optional<double>& released_bend = keyframes[1].bend;
    REQUIRE(released_bend.has_value());
    if (released_bend.has_value())
    {
        CHECK(std::is_eq(*released_bend <=> 0.0));
    }
    CHECK_FALSE(keyframes[1].fret.has_value());
    const std::optional<VibratoState>& ended_vibrato = keyframes[2].vibrato;
    REQUIRE(ended_vibrato.has_value());
    if (ended_vibrato.has_value())
    {
        CHECK(*ended_vibrato == VibratoState::Off);
    }
    CHECK_FALSE(keyframes[2].fret.has_value());
    // The document text itself, because that is where an elision would happen.
    CHECK(text.find(R"("bend": 0)") != std::string::npos);
    CHECK(text.find(R"("vibrato": "off")") != std::string::npos);
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

// The payload spellings the keyframe model replaced. `bend` still EXISTS under a different shape,
// so its refusal is keyed on the superseded shape rather than on the key: a document carrying it
// must name the re-import remedy instead of loading with its curve silently dropped. The others
// are simply gone — `slides` dissolved into the one interval-payload array, `waypoints` was that
// array's own earlier name, and `slideOut` became the keyframe at the ring's end, so BOTH of its
// spellings are refused: the object that carried an offset and the bare fret that replaced it.
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

    // The bare fret the object form was reduced to is gone with it: the gesture is a keyframe now,
    // so nothing on the note states it.
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

// The vibrato channel is a WIDTH axis, and the document says so in words: the ordinary shake is
// `"narrow"` — a description of what an ordinary vibrato physically is, a fraction of a semitone —
// and the deliberate exaggeration is `"wide"`. Absence is the only spelling of not shaking at an
// onset, which is what makes the third word legal only where the shake ENDS.
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
            // The keyframe channel states all THREE, the off value included: the shake ending is a
            // statement, so eliding it would delete the fact rather than shorten the record.
            note.keyframes = {
                Keyframe{.offset = Fraction{1}, .vibrato = VibratoState::Off},
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
            const std::optional<VibratoState>& ended = reparsed->notes[0].keyframes[0].vibrato;
            REQUIRE(ended.has_value());
            if (ended.has_value())
            {
                CHECK(*ended == VibratoState::Off);
            }
        }
    }

    SECTION("a note that does not shake writes no key at all")
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

    SECTION("a keyframe states all three, because that is where a shake can end")
    {
        CHECK(parse_note(R"("keyframes": [ { "offset": "1/2", "vibrato": "off" } ])").has_value());
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
// width: an open-coded `== Narrow` would answer "not shaking" for the wide notes it was never
// told about, exactly the trap isAccented exists to close on the emphasis axis.
TEST_CASE("Chart vibrato classifies every width above off", "[core][chart]")
{
    CHECK_FALSE(isShaking(VibratoState::Off));
    CHECK(isShaking(VibratoState::Narrow));
    CHECK(isShaking(VibratoState::Wide));
    // Value-initialization lands on not-shaking, which is why Off is declared first: a
    // default-constructed or resized note must not arrive already shaking.
    CHECK_FALSE(isShaking(VibratoState{}));
    CHECK_FALSE(isShaking(ChartNote{}.vibrato));
}

// A keyframe IS its statements: a location carrying none says nothing that could be drawn,
// played, or edited, yet it would shift every neighbour's index and survive every edit. The
// channels it may state are bounded too — a fret is a real position, and a bend is a PUSH, which
// a finger cannot make downward (W9-K).
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

    SECTION("a keyframe stating nothing is refused; one channel is enough")
    {
        refuses(Keyframe{.offset = Fraction{1, 2}});
        // The same location, one channel at a time: each is a legal record on its own, which is
        // what makes the refusal above about emptiness rather than about the offset.
        CHECK(validate_with(Keyframe{.offset = Fraction{1, 2}, .fret = 7}, 0.0).has_value());
        CHECK(validate_with(Keyframe{.offset = Fraction{1, 2}, .bend = 1.0}, 0.0).has_value());
        CHECK(
            validate_with(Keyframe{.offset = Fraction{1, 2}, .vibrato = VibratoState::Narrow}, 0.0)
                .has_value());

        // And refused where it has to be: on the LOAD path, which normalizes before it validates
        // (rock_song_package_read.cpp). Every strip arm in the normalizer clears channels and then
        // drops what it emptied, so a normalizer that instead swept up every empty keyframe it
        // found would repair this refusal out of existence for every document that carries one —
        // the assertion above would still pass and nothing would ever refuse the record.
        Chart loaded;
        loaded.tuning.strings = {"E2"};
        ChartNote empty_statement;
        empty_statement.position = GridPosition{.measure = 1, .beat = 1};
        empty_statement.string = 1;
        empty_statement.fret = 5;
        empty_statement.sustain = Fraction{1};
        empty_statement.keyframes = {Keyframe{.offset = Fraction{1, 2}}};
        loaded.notes = {empty_statement};
        static_cast<void>(normalizeChart(loaded, tempo_map));
        REQUIRE(loaded.notes.size() == 1);
        CHECK(loaded.notes[0].keyframes.size() == 1);
        const auto after_normalize = validateChartRules(loaded, tempo_map);
        REQUIRE_FALSE(after_normalize.has_value());
        CHECK(after_normalize.error().code == ChartErrorCode::InvalidNotePayload);
    }

    SECTION("offsets are strictly inside the ring, and offset zero is the onset's own")
    {
        // Zero would be a second spelling of a value the note itself already states.
        refuses(Keyframe{.offset = Fraction{}, .fret = 7});
        refuses(Keyframe{.offset = Fraction{3, 2}, .fret = 7});
        // The ring's own end is inclusive: a fret stated there is the RELEASE, the one statement
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

// A released ring stops clear of the next strike on its string, because its end is the release and
// no head may cover it. The clearance is the minimum sustain distance — a quarter beat in 4/4 — or
// half the gap where the gap is not longer than that. A plain ring still reaches the strike
// exactly.
// No keyframe crowds a head of its own string, whatever it states: the last keyframe stands at its
// clearance at the latest. The release IS the ring's end, so it moves by the ring shortening
// under it; any other statement moves alone and the ring keeps its length.
TEST_CASE("A last keyframe stands clear of the next strike on its string", "[core][chart]")
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
    const auto released = [&note_at](const Fraction sustain) {
        ChartNote note = note_at(GridPosition{.measure = 1, .beat = 1}, sustain, 5);
        setSlideOut(note, 3);
        return note;
    };

    SECTION("a release keeps the minimum sustain distance")
    {
        std::vector<ChartNote> notes{
            released(Fraction{2}),
            note_at(GridPosition{.measure = 1, .beat = 3}, Fraction{1}, 7),
        };
        // The ring itself is inside its bound: the truncation has nothing to do.
        CHECK(normalizeSustainOverlaps(notes, tempo_map).empty());
        CHECK(normalizeKeyframeClearances(notes, tempo_map) == std::vector<std::size_t>{0});
        CHECK(notes[0].sustain == Fraction{7, 4});
        const int* const release = slideOutFretOrNull(notes[0]);
        REQUIRE(release != nullptr);
        if (release != nullptr)
        {
            CHECK(*release == 3);
        }
        // The clipped ring is its own fixpoint.
        CHECK(normalizeKeyframeClearances(notes, tempo_map).empty());
    }
    SECTION("or half the gap where the gap is not longer than that")
    {
        std::vector<ChartNote> notes{
            released(Fraction{1, 8}),
            note_at(
                GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 8}}, Fraction{1}, 7),
        };
        CHECK(normalizeKeyframeClearances(notes, tempo_map) == std::vector<std::size_t>{0});
        CHECK(notes[0].sustain == Fraction{1, 16});
        CHECK(slideOutFretOrNull(notes[0]) != nullptr);
    }
    SECTION("never taking the statement before it")
    {
        // The margin line at 7/4 falls before the stop at 15/8, so the release halves the leg's
        // distance to the strike instead: 15/8 + 1/16 = 31/16, the stop kept.
        std::vector<ChartNote> notes{
            released(Fraction{2}),
            note_at(GridPosition{.measure = 1, .beat = 3}, Fraction{1}, 7),
        };
        notes[0].keyframes.insert(
            notes[0].keyframes.begin(), Keyframe{.offset = Fraction{15, 8}, .fret = 7});
        CHECK(normalizeKeyframeClearances(notes, tempo_map) == std::vector<std::size_t>{0});
        CHECK(notes[0].sustain == Fraction{31, 16});
        REQUIRE(notes[0].keyframes.size() == 2);
        CHECK(notes[0].keyframes[0].fret == 7);
        CHECK(slideOutFretOrNull(notes[0]) != nullptr);
    }
    SECTION("a keyframe placed inside the margin, short of the head, stands")
    {
        // The rule refuses the head, never proximity: a charter who wants the arrival an eighth
        // before the re-picked head gets exactly that, and the ring still reaches the head (the
        // adjacency a legato claim reads).
        std::vector<ChartNote> notes{
            note_at(GridPosition{.measure = 1, .beat = 1}, Fraction{2}, 5),
            note_at(GridPosition{.measure = 1, .beat = 3}, Fraction{1}, 7),
        };
        notes[0].keyframes = {Keyframe{.offset = Fraction{15, 8}, .fret = 7}};
        CHECK(normalizeSustainOverlaps(notes, tempo_map).empty());
        CHECK(normalizeKeyframeClearances(notes, tempo_map).empty());
        CHECK(notes[0].sustain == Fraction{2});
        REQUIRE(notes[0].keyframes.size() == 1);
        CHECK(notes[0].keyframes[0].offset == Fraction{15, 8});
    }
    SECTION("a stated fret the truncation carries onto the head is the release there")
    {
        // A fret at the ring's end IS the release (releaseKeyframe reads position), so a ring cut
        // back onto its own arrival falls away toward it, and the clearance then shortens the
        // ring under the release.
        std::vector<ChartNote> notes{
            note_at(GridPosition{.measure = 1, .beat = 1}, Fraction{3}, 5),
            note_at(GridPosition{.measure = 1, .beat = 3}, Fraction{1}, 7),
        };
        notes[0].keyframes = {Keyframe{.offset = Fraction{2}, .fret = 7}};
        CHECK(normalizeSustainOverlaps(notes, tempo_map) == std::vector<std::size_t>{0});
        CHECK(normalizeKeyframeClearances(notes, tempo_map) == std::vector<std::size_t>{0});
        CHECK(notes[0].sustain == Fraction{7, 4});
        const int* const release = slideOutFretOrNull(notes[0]);
        REQUIRE(release != nullptr);
        if (release != nullptr)
        {
            CHECK(*release == 7);
        }
    }
    SECTION("a bend curve's final point is bound like any other statement")
    {
        // An imported bend-release ends on the ring's end, which is the next head: the point
        // keeps its value and moves to the clearance.
        std::vector<ChartNote> notes{
            note_at(GridPosition{.measure = 1, .beat = 1}, Fraction{2}, 5),
            note_at(GridPosition{.measure = 1, .beat = 3}, Fraction{1}, 7),
        };
        notes[0].keyframes = {
            Keyframe{.offset = Fraction{1, 2}, .bend = 1.0},
            Keyframe{.offset = Fraction{2}, .bend = 0.0},
        };
        CHECK(normalizeKeyframeClearances(notes, tempo_map) == std::vector<std::size_t>{0});
        CHECK(notes[0].sustain == Fraction{2});
        REQUIRE(notes[0].keyframes.size() == 2);
        CHECK(notes[0].keyframes[1].offset == Fraction{7, 4});
        const std::optional<double>& moved_bend = notes[0].keyframes[1].bend;
        REQUIRE(moved_bend.has_value());
        if (moved_bend.has_value())
        {
            CHECK(std::is_eq(*moved_bend <=> 0.0));
        }
    }
    SECTION("a plain ring still reaches the strike exactly")
    {
        std::vector<ChartNote> notes{
            note_at(GridPosition{.measure = 1, .beat = 1}, Fraction{3}, 5),
            note_at(GridPosition{.measure = 1, .beat = 3}, Fraction{1}, 7),
        };
        CHECK(normalizeSustainOverlaps(notes, tempo_map) == std::vector<std::size_t>{0});
        CHECK(notes[0].sustain == Fraction{2});
        CHECK(normalizeKeyframeClearances(notes, tempo_map).empty());
    }
}

// Every strip arm the normalizer owns works per CHANNEL. A rule that refuses a glide has nothing
// to say about a bend or a shake authored at the same instant, and forgetting them because they
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

        // The discriminating twin: the same below-floor fret with nothing else stated leaves with
        // the keyframe, because a location with no statement left is no record at all.
        ChartNote bare = note_with({Keyframe{.offset = Fraction{1, 2}, .fret = 2}});
        bare.fret = 7;
        CHECK(normalizeChartNote(bare, capo_tuning).size() == 1);
        CHECK(bare.keyframes.empty());
    }

    SECTION("an open string loses its path and keeps its shake")
    {
        ChartNote note = note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7, .vibrato = VibratoState::Narrow}});
        note.fret = 0;
        setSlideOut(note, 9);
        const std::vector<ChartRepair> repairs = normalizeChartNote(note, tuning);
        REQUIRE(repairs.size() == 1);
        CHECK(repairs.front() == ChartRepair::OpenStringSlide);
        // The release went with the rest of the path: its keyframe stated a fret and nothing else,
        // so stripping the channel left no record at all.
        CHECK(slideOutFretOrNull(note) == nullptr);
        REQUIRE(note.keyframes.size() == 1);
        CHECK_FALSE(note.keyframes[0].fret.has_value());
        const std::optional<VibratoState>& kept_vibrato = note.keyframes[0].vibrato;
        REQUIRE(kept_vibrato.has_value());
        if (kept_vibrato.has_value())
        {
            CHECK(*kept_vibrato == VibratoState::Narrow);
        }
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
        // A shake or a push that CHANGES something is a statement whatever the fret says.
        Chart shaken;
        shaken.tuning = tuning;
        shaken.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{1}, .fret = 9}})};
        CHECK(normalizeChart(shaken, makeTempoMap()).empty());
    }

    SECTION("a bend value on a flat stretch of the curve says nothing")
    {
        // The shape every Guitar Pro bend imports as: a rise, a plateau, and a trailing repeat.
        // The plateau's END (3/4) is a statement — without it the curve would fall from 1/4
        // straight to the release at 1 — while its interior point (1/2) repeats both neighbours
        // and the trailing point past the release repeats the value the curve then holds.
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

    SECTION("a vibrato width the string already shakes at says nothing")
    {
        Chart chart;
        chart.tuning = tuning;
        chart.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 4}, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{1, 2}, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{3, 4}, .vibrato = VibratoState::Off}})};
        const std::vector<ChartConversion> conversions = normalizeChart(chart, makeTempoMap());
        REQUIRE(conversions.size() == 1);
        CHECK(conversions.front().repair == ChartRepair::SilentKeyframe);
        REQUIRE(chart.notes.front().keyframes.size() == 2);
        CHECK(chart.notes.front().keyframes[0].offset == Fraction{1, 4});
        CHECK(chart.notes.front().keyframes[1].offset == Fraction{3, 4});
        // Judged per channel: a repeated width beside a fret the path does not pass through is a
        // statement, and the keyframe stays whole.
        Chart stepped;
        stepped.tuning = tuning;
        stepped.notes = {note_with(
            {Keyframe{.offset = Fraction{1, 4}, .vibrato = VibratoState::Narrow},
             Keyframe{.offset = Fraction{1, 2}, .fret = 9, .vibrato = VibratoState::Narrow}})};
        CHECK(normalizeChart(stepped, makeTempoMap()).empty());
    }

    SECTION("a release states its fret and nothing else")
    {
        // A shake stated where the string is let go has no ring to sound in, so the load sheds
        // it and says so; the release itself stays.
        ChartNote note = note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7, .vibrato = VibratoState::Narrow}});
        note.sustain = Fraction{1, 2};
        const std::vector<ChartRepair> repairs = normalizeChartNote(note, tuning);
        REQUIRE(repairs.size() == 1);
        CHECK(repairs.front() == ChartRepair::ReleasePayload);
        REQUIRE(note.keyframes.size() == 1);
        CHECK_FALSE(note.keyframes[0].vibrato.has_value());
        const int* const release = slideOutFretOrNull(note);
        REQUIRE(release != nullptr);
        if (release != nullptr)
        {
            CHECK(*release == 7);
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
        CHECK_FALSE(isShaking(note.vibrato));
        CHECK(std::is_eq(note.bend <=> 0.0));
        REQUIRE(note.keyframes.size() == 1);
        const std::optional<int>& kept_fret = note.keyframes[0].fret;
        REQUIRE(kept_fret.has_value());
        if (kept_fret.has_value())
        {
            CHECK(*kept_fret == 7);
        }
        CHECK_FALSE(note.keyframes[0].bend.has_value());
        CHECK_FALSE(note.keyframes[0].vibrato.has_value());
    }

    SECTION("a saved scrape keeps its path and sheds the channels it overrides")
    {
        // savedChartNote is the memory-to-document seam: a scrape's turnarounds are pick travel,
        // so a bend or a shake riding one is exactly as latent as the note's own and never
        // reaches the file — while the fret statements, which ARE the path, survive.
        ChartNote note = note_with(
            {Keyframe{.offset = Fraction{1, 2}, .fret = 9, .bend = 1.0},
             Keyframe{.offset = Fraction{3, 4}, .vibrato = VibratoState::Narrow}});
        note.attack = NoteAttack::PickSlide;
        setSlideOut(note, 12);
        const ChartNote saved = savedChartNote(note);
        // The turnaround and the terminal survive — both are fret statements, which are the path —
        // while the shake-only keyframe leaves with the channel it carried.
        REQUIRE(saved.keyframes.size() == 2);
        const std::optional<int>& path_fret = saved.keyframes[0].fret;
        REQUIRE(path_fret.has_value());
        if (path_fret.has_value())
        {
            CHECK(*path_fret == 9);
        }
        CHECK_FALSE(saved.keyframes[0].bend.has_value());
        CHECK(slideOutFretOrNull(saved) != nullptr);
        // In memory the latents are untouched, which is what makes toggling the attack back
        // restore them.
        CHECK(note.keyframes.size() == 3);
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
    // A keyframe must carry a parseable offset — the release included, since it is one of them.
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
    // as zero would invent the one datum the model cannot derive, and every note of a chart
    // written before the duration model is missing exactly this.
    const auto missing = parse_note(R"("position": "1:1", "string": 1, "fret": 5)");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ChartErrorCode::MalformedDocument);
    CHECK(missing.error().message.find("sustain") != std::string::npos);
    // And the remedy is IN that message, because this is the path such a package actually takes:
    // the pre-model writer elided the key rather than writing a zero, so the positive-sustain
    // rule's own re-import sentence is unreachable from a real file.
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
    // attack path every other misspelling takes — no branch of its own, so a document written
    // before the token left fails exactly as `"attack": "wobble"` does.
    const auto none_attack = parse_note(
        R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1", "attack": "none")");
    REQUIRE_FALSE(none_attack.has_value());
    CHECK(none_attack.error().code == ChartErrorCode::MalformedDocument);
    CHECK(
        none_attack.error().message.find("chart note attack is unknown: none") !=
        std::string::npos);
}

// Emphasis is one axis with a never-written default, so three things have to hold together: both
// named values load, the default is spelled by ABSENCE, and the bool this replaced is refused
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
    // harmonic/touch keys do. Delete this with the tripwire once the corpus is re-imported.
    CHECK_FALSE(
        parse_note(R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "accent": true)")
            .has_value());
    CHECK_FALSE(
        parse_note(
            R"("position": "1:1", "string": 1, "fret": 5, "sustain": "1/8", "accent": false)")
            .has_value());

    // Round trip: both named values survive a write and read, and a normal note writes no key at
    // all — which is what keeps a package that predates the axis byte-identical after a save.
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

    // The tripwire: the single "mute" key this pair replaced fails the load and names the fix,
    // exactly as the removed accent and harmonic/touch keys do. A silently ignored key would load
    // every muted note in the corpus as unmuted. Delete this once the corpus is re-imported.
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

// The WHOLE hand window must fit on the neck: bounding only the index finger let a wide hand run
// off the end. With g_max_fret = 24, fret 20 width 5 spans 20..24 and stands, while fret 22
// width 5 wants 22..26 and is refused.
TEST_CASE("Chart rules bound the whole hand window by the neck", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart = makeFullChart();
    REQUIRE_FALSE(chart.fret_hand_positions.empty());

    chart.fret_hand_positions.back().fret = 20;
    chart.fret_hand_positions.back().width = 5;
    CHECK(validateChartRules(chart, tempo_map).has_value());

    chart.fret_hand_positions.back().fret = 22;
    CHECK_FALSE(validateChartRules(chart, tempo_map).has_value());
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
    slide_past_sustain.notes[2].keyframes.back().offset = Fraction{3};
    const auto slide_result = validateChartRules(slide_past_sustain, tempo_map);
    REQUIRE_FALSE(slide_result.has_value());
    CHECK(slide_result.error().code == ChartErrorCode::InvalidNotePayload);

    // A negative fret is refused wherever it is stated, the release included — there is no second
    // rule for the fret the hand leaves toward, because it is a keyframe's fret like any other.
    Chart negative_release = makeFullChart();
    setSlideOut(negative_release.notes[2], -1);
    const auto negative_result = validateChartRules(negative_release, tempo_map);
    REQUIRE_FALSE(negative_result.has_value());
    CHECK(negative_result.error().code == ChartErrorCode::InvalidNotePayload);

    // A keyframe sitting on a later onset of its string is NORMALIZED, never refused: the load
    // repair moves it back to its clearance (normalizeKeyframeClearances), so the validator, which
    // every producer runs after that repair, accepts the chart as it stands. The ring here runs
    // PAST the landing, so the truncation carries the statement to the ring's end, where a stated
    // fret IS the release; the clearance then shortens the ring under it to one margin short of
    // the landing — a twelfth, the third-beat gap less the quarter-beat margin.
    Chart keyframe_on_onset = makeFullChart();
    keyframe_on_onset.notes[5].sustain = Fraction{1, 2};
    keyframe_on_onset.notes[5].keyframes = {Keyframe{.offset = Fraction{1, 3}, .fret = 5}};
    CHECK(validateChartRules(keyframe_on_onset, tempo_map).has_value());
    const std::vector<ChartConversion> moved = normalizeChart(keyframe_on_onset, tempo_map);
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].repair == ChartRepair::OverlappingTail);
    CHECK(moved[1].repair == ChartRepair::CrowdedKeyframe);
    CHECK(keyframe_on_onset.notes[5].sustain == Fraction{1, 12});
    REQUIRE(keyframe_on_onset.notes[5].keyframes.size() == 1);
    CHECK(keyframe_on_onset.notes[5].keyframes[0].offset == Fraction{1, 12});
    CHECK(slideOutFretOrNull(keyframe_on_onset.notes[5]) != nullptr);

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
        // Nothing is pressed to travel, so a fret-0 glide or trail-off refuses, and the normalizer
        // drops the path whole — the refusal IS the fixpoint of that repair, which is what lets a
        // load repair the form the gate refuses.
        ChartNote open_slide = make_note(1, 1, 0);
        open_slide.sustain = Fraction{1};
        open_slide.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 5}};
        CHECK_FALSE(validate({open_slide}).has_value());
        ChartNote shed_slide = open_slide;
        CHECK(
            normalizeChartNote(shed_slide, ChartTuning{}) ==
            std::vector<ChartRepair>{ChartRepair::OpenStringSlide});
        CHECK(shed_slide.keyframes.empty());
        CHECK(validate({shed_slide}).has_value());

        ChartNote open_exit = make_note(1, 1, 0);
        open_exit.sustain = Fraction{1};
        setSlideOut(open_exit, 5);
        CHECK_FALSE(validate({open_exit}).has_value());
        static_cast<void>(normalizeChartNote(open_exit, ChartTuning{}));
        CHECK(slideOutFretOrNull(open_exit) == nullptr);

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
        // the capo. The importer degrades such a glide to the unpitched trail-off instead.
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

        // A pitched note's unpitched trail-off exits above the capo too.
        ChartNote trail_off = make_note(1, 1, 7);
        trail_off.sustain = Fraction{1};
        setSlideOut(trail_off, 3);
        CHECK(validate({trail_off}).has_value());
        CHECK_FALSE(validate({trail_off}, 3).has_value());
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

        // The open-string tap harmonic strikes the node itself, so a node satisfies the rule.
        tap.harmonic_node = 12.0;
        CHECK(validate({tap}).has_value());

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
        // than what rings, which is the same reading that lets a dead note keep its fret.
        ChartNote muted_harmonic = dead;
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
        // have is a drawn tail — that is presentedChartNotes rule 4, covered in
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

        // A dead tap harmonic still sheds the tremolo the damping finger cannot hold — and that
        // is the ONLY repair it takes: the shed never drags the ring away with it.
        ChartNote dead_tap_harmonic = make_note(1, 1, 5);
        dead_tap_harmonic.dead = true;
        dead_tap_harmonic.attack = NoteAttack::Tap;
        dead_tap_harmonic.harmonic_node = 17.0;
        dead_tap_harmonic.tremolo = true;
        dead_tap_harmonic.sustain = Fraction{1};
        CHECK(
            normalizeChartNote(dead_tap_harmonic, ChartTuning{}) ==
            std::vector<ChartRepair>{ChartRepair::TapHarmonicTremolo});
        CHECK(dead_tap_harmonic.sustain == Fraction{1});
    }

    SECTION("a note must ring: a non-positive sustain is refused, never repaired")
    {
        // Structural, because no repair can invent a duration — and it doubles as the format
        // tripwire, since a chart written before the duration model stores zero for every
        // tail-less note.
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
        // pressing, so bending it is ordinary work.
        ChartNote fretted = make_note(1, 1, 5);
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
        // The release is clamped by the same keyframe rule, with no clause of its own.
        const int* const clamped_release = slideOutFretOrNull(past);
        REQUIRE(clamped_release != nullptr);
        if (clamped_release != nullptr)
        {
            CHECK(*clamped_release == g_max_fret);
        }
        CHECK(valid(past));
    }

    SECTION("slide positions on or below the capo lift above it, keyframes drop")
    {
        // A glide's stops are pressed positions and a scrape's turnarounds are pick travel, so
        // neither may name the open string or a capo'd fret: a PITCHED keyframe there names
        // nothing pressed and drops, while the release is a direction as much as a fret — a fall
        // toward the floor is still a fall — so it lifts onto the floor instead.
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
        // The surviving stop and the lifted release: the dropped one stated nothing else, so it
        // left with its fret.
        REQUIRE(glide.keyframes.size() == 2);
        CHECK(glide.keyframes.front().fret == 7);
        const int* const lifted_release = slideOutFretOrNull(glide);
        REQUIRE(lifted_release != nullptr);
        if (lifted_release != nullptr)
        {
            CHECK(*lifted_release == tuning.capo + 1);
        }
        CHECK(valid(glide));

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
        CHECK(scrape.keyframes.empty());
        CHECK(slideOutFretOrNull(scrape) == nullptr);
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
        FretHandPosition low{
            .position = GridPosition{.measure = 1, .beat = 1}, .fret = 2, .width = 4
        };
        CHECK(
            normalizeFretHandPosition(low, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretBelowCapo});
        CHECK(low.fret == tuning.capo + 1);
        CHECK(low.width == 4);

        // Running off the end: slides down until the whole width fits under the last fret.
        FretHandPosition high{
            .position = GridPosition{.measure = 1, .beat = 1}, .fret = 23, .width = 4
        };
        CHECK(
            normalizeFretHandPosition(high, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretPastBoard});
        CHECK(high.fret == g_max_fret - 3);
        CHECK(high.width == 4);

        // Wider than the frets above the capo: the width shrinks first, so the placement that
        // follows always succeeds and never pushes the finger back below the capo.
        FretHandPosition wide{
            .position = GridPosition{.measure = 1, .beat = 1}, .fret = 1, .width = 40
        };
        CHECK(
            normalizeFretHandPosition(wide, tuning) ==
            std::vector<ChartRepair>{ChartRepair::FretPastBoard, ChartRepair::FretBelowCapo});
        CHECK(wide.width == g_max_fret - tuning.capo);
        CHECK(wide.fret == tuning.capo + 1);
        CHECK(normalizeFretHandPosition(wide, tuning).empty());
    }

    SECTION("the whole chart normalizes in one call, with the settle sweep last")
    {
        // Every stage in one call, in order: the stream-level ring bound (40-Q2-B) after the
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
            FretHandPosition{
                .position = GridPosition{.measure = 1, .beat = 1}, .fret = 1, .width = 4
            });
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

    SECTION("the released fret picks the direction, and equal frets justify nothing")
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

    SECTION("the released fret is where the finger ENDS")
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

        // Reaching the onset exactly is reaching — which is the longest ring 40-Q2-B allows, so
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
        // The scrape holds exactly what it draws, its whole one-beat gesture: a released ring
        // never trims. A number of its own is what says the two zeros above are the mute rather
        // than a span that failed to cover the onset.
        CHECK(memory_resolutions.holds[1] == Fraction{1});
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
        // local. Here an equal released fret justifies nothing, and the tap resolves anyway.
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
        // last note connects to is the middle note's STORED released fret and ring, never the
        // middle note's own answer — so a broken claim is not contagious, which is what lets the
        // sweep flatten in a single pass and lets display read the table with no fixed point to
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
    // terminal is not one of them — it is the release the path must still end on — so the clear
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
    clipPayloadsToSustain(longer_ring.notes[scrape], Fraction{3, 2});
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

    // No keyframe sits on a head of its own string, and a scrape's terminal is no exception: it is
    // the release, whose chip a head there would cover, so a terminal on the next onset is refused
    // (the load repair moves one back to its clearance first). An interior turnaround on a later
    // onset is rejected the same way.
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
    // A terminal on the next head is normalized like every keyframe on a head: the scrape's ring
    // shortens under it to the clearance, and the chart then validates.
    CHECK(validateChartRules(terminal_on_onset, tempo_map).has_value());
    const std::vector<ChartConversion> terminal_moved =
        normalizeChart(terminal_on_onset, tempo_map);
    REQUIRE(terminal_moved.size() == 1);
    CHECK(terminal_moved.front().repair == ChartRepair::CrowdedKeyframe);
    CHECK(terminal_on_onset.notes[0].sustain == Fraction{1, 4});
    CHECK(slideOutFretOrNull(terminal_on_onset.notes[0]) != nullptr);

    // An interior stop on the head with the ring running past it: the truncation makes that stop
    // the terminal (a fret at the ring's end is the release) and the clearance shortens under it.
    Chart interior_on_onset = terminal_on_onset;
    interior_on_onset.notes[0].sustain = Fraction{1};
    interior_on_onset.notes[0].keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 4}};
    setSlideOut(interior_on_onset.notes[0], 9);
    const std::vector<ChartConversion> interior_moved =
        normalizeChart(interior_on_onset, tempo_map);
    REQUIRE(interior_moved.size() == 2);
    CHECK(interior_moved[0].repair == ChartRepair::OverlappingTail);
    CHECK(interior_moved[1].repair == ChartRepair::CrowdedKeyframe);
    CHECK(interior_on_onset.notes[0].sustain == Fraction{1, 4});
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
    CHECK_FALSE(isShaking(saved.vibrato));
    CHECK_FALSE(saved.palm_mute);
    CHECK_FALSE(saved.dead);
    CHECK(saved.emphasis == NoteEmphasis::Accent);
    const int* const saved_release = slideOutFretOrNull(saved);
    REQUIRE(saved_release != nullptr);
    if (saved_release != nullptr)
    {
        CHECK(*saved_release == 9);
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
    // states its own two strings whole and stays a box. This is where "a ringing string outside the
    // posture" went — a fretting-hand ring across a start is always folded IN, so the only ring
    // that can cross one and leave the shape alone is the other hand's.
    chart.notes[0].sustain = Fraction{2};
    Chart tapped_before = chart;
    tapped_before.notes[0].attack = NoteAttack::Tap;
    CHECK_FALSE(arrivesAsArpeggio(tapped_before.notes, strum_at, tempo_map));

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
            .sustain = g_fixture_ring,
            .bend = 0.0,
            .keyframes = {},
        });
    CHECK(arrivesAsArpeggio(chord_sourced_ring.notes, strum_at, tempo_map));

    // F1: a DEAD string's carry classifies. The class is a fact about the HANDS — the finger is
    // still down and the strum still picks around it — so it reads the STORED ring, the same one
    // the walk's fold-in reads. E25 takes the tail off what a surface DRAWS and says nothing about
    // what the hands were doing; letting it decide here would flip the span at an interior slot
    // and not at its start.
    Chart dead_ring = chart;
    dead_ring.notes[0].dead = true;
    CHECK(arrivesAsArpeggio(dead_ring.notes, strum_at, tempo_map));
}

// WHICH FIELD A CLAIM COMES OUT OF, asked at the two functions that own the answer. A claim is the
// fretting hand's stop under an onset the OTHER hand made, and where it is written down depends on
// what the picking hand is doing at the fret: stopping the string itself, so the hand's own stop is
// the separate `held`; or only touching a node, so the stop the string speaks from — `fret` — is
// the fretting hand's already — and where that stop is the OPEN string the hand presses nothing, so
// there is no claim at all. Everything span-scoped reads the one query, which is why these answers
// are worth pinning apart from the surfaces that consume them.
TEST_CASE("A claim reads the held stop or the fret, by what the picking hand does", "[core][chart]")
{
    const auto note_with = [](const NoteAttack attack,
                              const int fret,
                              const std::optional<int>
                                  held,
                              const std::optional<double>
                                  node) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 3;
        note.fret = fret;
        note.sustain = Fraction{1, 4};
        note.attack = attack;
        note.held = held;
        note.harmonic_node = node;
        return note;
    };

    SECTION("a plain tap and a scrape stop the string, so the claim is the planted finger")
    {
        CHECK(pickingHandStopsString(NoteAttack::Tap, std::nullopt));
        CHECK(claimedStop(note_with(NoteAttack::Tap, 17, 5, std::nullopt)) == std::optional{5});
        CHECK(pickingHandStopsString(NoteAttack::PickSlide, std::nullopt));
        CHECK(
            claimedStop(note_with(NoteAttack::PickSlide, 5, 11, std::nullopt)) ==
            std::optional{11});
    }

    SECTION("a tapped harmonic over a pressed stop claims that stop, not a planted finger")
    {
        CHECK_FALSE(pickingHandStopsString(NoteAttack::Tap, std::optional{17.0}));
        CHECK(claimedStop(note_with(NoteAttack::Tap, 5, std::nullopt, 17.0)) == std::optional{5});
        // And the FRET is the answer whatever a latent field left behind by an earlier form still
        // says, which is what makes this a query about the record's shape rather than about which
        // members happen to be set.
        CHECK(claimedStop(note_with(NoteAttack::Tap, 5, 3, 17.0)) == std::optional{5});
    }

    SECTION("a tapped harmonic over the OPEN string claims nothing, exactly as a natural does")
    {
        // Every harmonic is one record, and this one is a NATURAL whose node the picking hand
        // happens to touch: the fretting hand presses nothing, so there is no stop to state beside
        // the head (\ref harmonicOverPressedStop).
        CHECK_FALSE(harmonicOverPressedStop(note_with(NoteAttack::Tap, 0, std::nullopt, 12.0)));
        CHECK_FALSE(claimedStop(note_with(NoteAttack::Tap, 0, std::nullopt, 12.0)).has_value());
        // And a latent field an earlier form left behind changes nothing, exactly as above.
        CHECK_FALSE(claimedStop(note_with(NoteAttack::Tap, 0, 3, 12.0)).has_value());
    }

    SECTION("a fretting-hand onset claims nothing, harmonic or not")
    {
        // Its own fret is already the hand's stop, so there is no second stop to state — and an
        // artificial harmonic is exactly that note with a node above the fret it presses.
        CHECK_FALSE(pickingHandStopsString(NoteAttack::Pick, std::nullopt));
        CHECK_FALSE(
            claimedStop(note_with(NoteAttack::Pick, 5, std::nullopt, std::nullopt)).has_value());
        CHECK_FALSE(pickingHandStopsString(NoteAttack::Pick, std::optional{17.0}));
        CHECK_FALSE(claimedStop(note_with(NoteAttack::Pick, 5, std::nullopt, 17.0)).has_value());
    }
}

// The held stop's format and its two validity rules. The field is the one way the chart can say
// what the FRETTING hand is doing under an onset the other hand made, so what must hold is that
// absence stays a meaning, that the attacks it is legal on are exactly the ones the picking hand
// stops the string for, and that a stop lying anywhere in the onset's own travel — which no hand
// can play — is refused rather than saved.
TEST_CASE("Chart document round-trips the held stop under a right-hand onset", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const auto make_chart = [](ChartNote note) {
        Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {std::move(note)};
        return chart;
    };
    const auto tap_holding = [](const std::optional<int> held) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = 1};
        note.string = 3;
        note.fret = 12;
        note.sustain = Fraction{1, 4};
        note.attack = NoteAttack::Tap;
        note.held = held;
        return note;
    };

    SECTION("a stated stop survives the round trip and an absent one writes nothing")
    {
        const Chart chart = make_chart(tap_holding(5));
        const std::string text = chartDocumentText(chart, tempo_map);
        CHECK(text.find(R"("held": 5)") != std::string::npos);
        const auto parsed = parseChartDocument(text);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->notes.size() == 1);
        CHECK(parsed->notes.front() == chart.notes.front());

        // Absence is the meaning "the hand states no stop of its own", so the key is elided —
        // and fret 0 is NOT that absence, because an open string a voicing deliberately leaves is
        // a real statement. Both halves, so an elision keyed on the value could not pass.
        const std::string silent = chartDocumentText(make_chart(tap_holding({})), tempo_map);
        CHECK(silent.find(R"("held")") == std::string::npos);
        const std::string open = chartDocumentText(make_chart(tap_holding(0)), tempo_map);
        CHECK(open.find(R"("held": 0)") != std::string::npos);
        const auto open_parsed = parseChartDocument(open);
        REQUIRE(open_parsed.has_value());
        REQUIRE(open_parsed->notes.size() == 1);
        CHECK(open_parsed->notes.front().held == std::optional{0});
    }

    SECTION("only a plain tap or a pick slide may carry one")
    {
        // The two onsets the picking hand stops the string for keep it; every other attack is
        // refused, because there the note's own fret already IS the fretting hand's stop.
        for (const NoteAttack attack : {NoteAttack::Tap, NoteAttack::PickSlide})
        {
            ChartNote note = tap_holding(5);
            note.attack = attack;
            if (isScrape(attack))
            {
                // A scrape's own required shape, so the case tests the held rule and not this one.
                setSlideOut(note, 17);
            }
            // Hoisted out of the assertion: a Catch2 macro mentions its expression a second time
            // in the never-run short-circuit clause, and a `std::move` there reads as a use after
            // the move to the CI-only checker.
            const auto accepted = validateChartRules(make_chart(std::move(note)), tempo_map);
            CHECK(accepted.has_value());
        }
        for (const NoteAttack attack :
             {NoteAttack::Pick, NoteAttack::Legato, NoteAttack::LeftTap, NoteAttack::Pinch})
        {
            ChartNote note = tap_holding(5);
            note.attack = attack;
            if (attack == NoteAttack::Pinch)
            {
                note.harmonic_node = 17.0;
            }
            const auto refused = validateChartRules(make_chart(std::move(note)), tempo_map);
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().message.find("held") != std::string::npos);
        }
    }

    SECTION("a stop inside the onset's own travel is physically impossible and is refused")
    {
        // The travel-range rule, which the equal-fret refusal is the degenerate case of: the
        // planted finger is on the string, so the onset cannot start on it, end on it, or pass
        // through it.
        //
        // An onset that states NO path has a hull of one point, which is the shipped equal-fret
        // refusal as the degenerate case. This tap states none.
        const auto tapped_on_the_finger =
            validateChartRules(make_chart(tap_holding(12)), tempo_map);
        REQUIRE_FALSE(tapped_on_the_finger.has_value());
        CHECK(tapped_on_the_finger.error().message.find("pass through") != std::string::npos);
        // The control, one fret away: the refusal is about the stop lying in the travel, not about
        // the value.
        CHECK(validateChartRules(make_chart(tap_holding(11)), tempo_map).has_value());

        // A SCRAPE travels its whole path, so the range is the closed hull of the start, every
        // keyframe it turns at, and the terminal it releases on. Built to travel 5 -> 15 -> 9,
        // whose hull is [5, 15].
        const auto scrape_holding = [](const std::optional<int> held) {
            ChartNote note;
            note.position = GridPosition{.measure = 1, .beat = 1};
            note.string = 3;
            note.fret = 5;
            note.sustain = Fraction{1, 2};
            note.attack = NoteAttack::PickSlide;
            note.keyframes = {Keyframe{.offset = Fraction{1, 4}, .fret = 15}};
            setSlideOut(note, 9);
            note.held = held;
            return note;
        };
        // Each end of the path and one fret strictly inside it: the pick cannot start on the
        // finger, end on it, or run over it on the way.
        for (const int planted : {5, 9, 15, 7, 12})
        {
            const auto refused = validateChartRules(make_chart(scrape_holding(planted)), tempo_map);
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().message.find("pass through") != std::string::npos);
        }
        // And the controls the refusals need: a finger safely OUTSIDE the hull on either side is a
        // legal record, which is what proves the rule reads the path rather than refusing every
        // held stop a scrape carries.
        for (const int planted : {0, 4, 16})
        {
            CHECK(validateChartRules(make_chart(scrape_holding(planted)), tempo_map).has_value());
        }

        // And it is the PATH the rule reads, never the attack: a tap the charter gave keyframes
        // and a slide-out travels exactly as the scrape above does, over the same 5 -> 15 -> 9
        // hull, so the same range binds it. The discrimination for the one-point case at the top
        // of this section, which would otherwise pass for a tap-shaped reason.
        const auto travelling_tap = [](const std::optional<int> held) {
            ChartNote note;
            note.position = GridPosition{.measure = 1, .beat = 1};
            note.string = 3;
            note.fret = 5;
            note.sustain = Fraction{1, 2};
            note.attack = NoteAttack::Tap;
            note.keyframes = {Keyframe{.offset = Fraction{1, 4}, .fret = 15}};
            setSlideOut(note, 9);
            note.held = held;
            return note;
        };
        const auto crossed = validateChartRules(make_chart(travelling_tap(12)), tempo_map);
        REQUIRE_FALSE(crossed.has_value());
        CHECK(crossed.error().message.find("pass through") != std::string::npos);
        CHECK(validateChartRules(make_chart(travelling_tap(16)), tempo_map).has_value());
    }

    SECTION("the board and the capo bind it exactly as they bind a fret")
    {
        Chart capoed = make_chart(tap_holding(2));
        capoed.tuning.capo = 3;
        CHECK_FALSE(validateChartRules(capoed, tempo_map).has_value());
        // Fret 0 under the same capo is the capo'd open string, which is legal for a held stop
        // exactly as it is for a fret.
        capoed.notes.front().held = 0;
        CHECK(validateChartRules(capoed, tempo_map).has_value());
        // Past the board is the NORMALIZER's clamp, not a refusal, so it is asked of the note.
        ChartNote past = tap_holding(g_max_fret + 6);
        const std::vector<ChartRepair> repairs = normalizeChartNote(past, ChartTuning{});
        CHECK(past.held == std::optional{g_max_fret});
        CHECK(std::ranges::find(repairs, ChartRepair::FretPastBoard) != repairs.end());
    }

    SECTION("a wrong-typed key is malformed rather than absent")
    {
        const auto parsed = parseChartDocument(
            R"({ "formatVersion": 1, "tuning": { "strings": ["E2"] },)"
            R"( "notes": [ { "position": "1:1", "string": 1, "fret": 12, "sustain": "1/4",)"
            R"( "attack": "tap", "held": "5" } ] })");
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().message.find("held") != std::string::npos);
    }
}

// The record a tap harmonic IS: the fretting hand presses a stop and the tapping finger touches a
// node above it, so one note states the stop the string speaks from and the point the touch lies
// at — the same two facts an artificial harmonic states, in the same two fields. Three layers have
// to agree about it — the rules, the derivation and the document — and every one of them reads the
// pressed stop out of `fret` (\ref physicalStopFret). A planted finger is a third fact this record
// cannot carry, and the saved-form fixpoint is what says so.
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

    SECTION("both facts coexist, and the node is judged against the note's own fret")
    {
        CHECK(validateChartRules(chart, tempo_map).has_value());

        // The rule binds — a node lies on the speaking length, so it cannot sit at or behind the
        // stop — and the stop it binds against is this note's fret. Both arms move with that fret,
        // which is what makes them a discrimination rather than a restatement: a node level with
        // the pressed 5 is refused, and one between the pressed stop and the tapped point is legal.
        ChartNote behind = tapped_harmonic();
        behind.harmonic_node = 5.0;
        const auto refused = validateChartRules(make_chart({behind}), tempo_map);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message.find("beyond the stop") != std::string::npos);

        ChartNote inside = tapped_harmonic();
        inside.harmonic_node = 10.0;
        CHECK(validateChartRules(make_chart({inside}), tempo_map).has_value());

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

    SECTION("a planted finger beside the node is no part of the record")
    {
        // A `held` on this note would be a second stop on a string the fretting hand is already
        // pressing at `fret`, so the writer strips it and the saved-form fixpoint refuses the
        // in-memory record that still carries one — the same gate a pick slide's pitched latents
        // pass through, asked once for every field an attack may not state.
        ChartNote planted = tapped_harmonic();
        planted.held = 3;
        CHECK_FALSE(savedChartNote(planted).held.has_value());
        const auto refused = validateChartRules(make_chart({planted}), tempo_map);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message.find("held") != std::string::npos);

        // The control that keeps the strip about the NODE rather than about the tap: the same
        // attack with nothing touched keeps the finger the charter typed.
        ChartNote plain = tapped_harmonic();
        plain.fret = 17;
        plain.harmonic_node.reset();
        plain.held = 5;
        CHECK(savedChartNote(plain).held == std::optional{5});
        CHECK(validateChartRules(make_chart({plain}), tempo_map).has_value());
    }

    SECTION("the claim joins the shape's posture, and its stop shows in the satellite")
    {
        Arrangement arrangement;
        arrangement.chart = chart;
        const ChartViewState state = makeChartViewState(arrangement, tempo_map);

        // Two members open the span — the struck stop and the tap's claim, which is the very fret
        // this note presses — so the pressed 5 joins the posture it founds.
        REQUIRE(state.shapes.size() == 1);
        const ShapeViewState& shape = state.shapes.front();
        REQUIRE(shape.strings.size() == 2);
        // The struck member's own head already prints that very fret, so the bracket states
        // nothing beside it; the tap's stop is displaced outboard, because the head at that slot
        // prints the node it touches rather than the stop it presses.
        CHECK(
            shape.strings[0] ==
            ShapeStringViewState{.string = 1, .stop = frettedStop(7), .digit = std::nullopt});
        CHECK(
            shape.strings[1] ==
            ShapeStringViewState{
                .string = 3, .stop = frettedStop(5), .digit = StopMarkSlot::Satellite
            });

        // The tap's own mark, which is what makes the displaced digit reachable: at the bracket it
        // was printed under, in the column it was printed in — and POSTURE ink, because this tap
        // FRONTS the bracket, so the span's own furniture states the stop and it stands there
        // whatever its authorship.
        const auto tap = std::ranges::find(state.notes, 3, &NoteViewState::string);
        REQUIRE(tap != state.notes.end());
        const std::optional<StopMarkViewState>& mark = tap->stop_mark;
        REQUIRE(mark.has_value());
        if (mark.has_value())
        {
            CHECK(mark->face == StopMarkFace::Posture);
            CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(shape.start_seconds, 1e-9));
        }
        CHECK(tap->held == std::optional{5});
    }

    SECTION("the document carries both, and the settle leaves the record alone")
    {
        const std::string text = chartDocumentText(chart, tempo_map);
        CHECK(text.find(R"("fret": 5)") != std::string::npos);
        CHECK(text.find(R"("harmonicNode": 17)") != std::string::npos);
        CHECK(text.find(R"("held")") == std::string::npos);
        const auto parsed = parseChartDocument(text);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->notes.size() == chart.notes.size());
        CHECK(parsed->notes == chart.notes);

        // The settle judges what states nothing, and nothing here does: both claims reach the span.
        // It is also the sweep's whole reach that this record is out of — it takes the `held`
        // FIELD, and this note has none to take.
        std::vector<ChartNote> settled = chart.notes;
        CHECK(sweepInertClaimedStops(settled, tempo_map).empty());
        CHECK(settled == chart.notes);
    }
}

// DERIVED HELD, the document half. A stored held stop a pull-off already states is a second
// spelling of one fact, so the normalizer takes it on every load and the writer therefore never
// emits it — and because the derivation does not read the field it clears, nothing the chart states
// moves.
TEST_CASE("The normalizer clears a held stop a pull-off states", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // A tap sounding fret 12 on string 1 with a fretting stop under it, and the note a beat later
    // at fret 5. Where that note CLAIMS legato it resolves to a pull off the tap, which is the
    // statement that a finger was waiting on 5.
    const auto figure = [&tempo_map](const NoteAttack successor_attack, const int stored_held) {
        Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        ChartNote tap;
        tap.position = GridPosition{.measure = 1, .beat = 1};
        tap.string = 1;
        tap.fret = 12;
        tap.sustain = Fraction{1};
        tap.attack = NoteAttack::Tap;
        tap.held = stored_held;
        // A string ringing underneath, so the tap's claim reaches a span and the INERT sweep has
        // no reason of its own to take the field: what clears it below has to be the derivation.
        ChartNote drone;
        drone.position = GridPosition{.measure = 1, .beat = 1};
        drone.string = 2;
        drone.fret = 7;
        drone.sustain = Fraction{4};
        ChartNote successor;
        successor.position = GridPosition{.measure = 1, .beat = 2};
        successor.string = 1;
        successor.fret = 5;
        successor.sustain = Fraction{1};
        successor.attack = successor_attack;
        chart.notes = {tap, drone, successor};
        REQUIRE(validateChartRules(chart, tempo_map).has_value());
        return chart;
    };

    SECTION("the field goes, reported, and the stop stays exactly where it was")
    {
        Chart chart = figure(NoteAttack::Legato, 7);
        const std::vector<ChartConversion> conversions = normalizeChart(chart, tempo_map);
        REQUIRE(conversions.size() == 1);
        CHECK(conversions.front().repair == ChartRepair::DerivedHeldStop);
        CHECK_FALSE(chart.notes.front().held.has_value());
        // The whole point: the resolution is unchanged, because it never read the field.
        CHECK(
            chartClaimedStops(chartConnections(chart.notes, tempo_map)).front() ==
            std::optional{5});
        // One pass reaches the fixpoint, like every other rule the normalizer owns.
        CHECK(normalizeChart(chart, tempo_map).empty());
    }

    SECTION("no pull-off, no residue: the stored value is the authority and stays")
    {
        Chart chart = figure(NoteAttack::Pick, 7);
        CHECK(normalizeChart(chart, tempo_map).empty());
        CHECK(chart.notes.front().held == std::optional{7});
        CHECK(
            chartClaimedStops(chartConnections(chart.notes, tempo_map)).front() ==
            std::optional{7});
    }
}

// AND THE DERIVATION IS BOUND BY THE ONSET'S OWN TRAVEL, through the very predicate the document
// refuses an AUTHORED held stop by (`travelsThroughFret`). A planted finger is on the string for
// the whole of the picking hand's path, so a stop that path sweeps over is a stop nothing could
// have been waiting on — and a derivation free of that bound would state values the same rules
// reject.
TEST_CASE("A pull-off states nothing inside the onset's traveled range", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // A tap and the note that pulls off it onto fret 5, differing in ONE thing: whether the tap's
    // own path passes through 5. The RELEASED fret is 12 either way, so both resolve the same pull
    // and both offer the same candidate stop.
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

    SECTION("a tap keyframed up past the fret states nothing about it")
    {
        // The hull runs 3 through 12 and 5 sits inside it, so the sounding path crossed the very
        // stop the connection would credit to a waiting finger.
        const Chart chart = figure(3);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK_FALSE(chartDerivedStops(connections).front().has_value());
        CHECK_FALSE(chartClaimedStops(connections).front().has_value());
    }

    SECTION("the untravelled tap still states it")
    {
        // The discrimination: one keyframe apart, and the hull collapses to the tap's own point.
        const Chart chart = figure(std::nullopt);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartDerivedStops(connections).front() == std::optional{5});
        CHECK(chartClaimedStops(connections).front() == std::optional{5});
    }
}

// THE HOLD-UNDER LAW's derivation half: the planted stop is a fact about EVERY pull-off source,
// whichever hand made its onset, while the held FIELD's scope — and with it the claim column, the
// satellites, the editor's refusals and the writer's sweeps — stays the narrower one. The first
// section IS the narrow form: it is the test that fails first if anyone ever collapses
// chartPlantedStops and chartDerivedStops into one function.
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

    SECTION("a fretting-hand source plants wide and claims nothing")
    {
        const Chart chart = figure(NoteAttack::Pick, 5);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartPlantedStops(connections).front() == std::optional{5});
        CHECK_FALSE(chartDerivedStops(connections).front().has_value());
        CHECK_FALSE(chartClaimedStops(connections).front().has_value());
    }

    SECTION("a tap source plants and claims alike")
    {
        const Chart chart = figure(NoteAttack::Tap, 5);
        const ChartConnections connections = chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato[1] == LegatoMotion::Pull);
        CHECK(chartPlantedStops(connections).front() == std::optional{5});
        CHECK(chartDerivedStops(connections).front() == std::optional{5});
        CHECK(chartClaimedStops(connections).front() == std::optional{5});
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
