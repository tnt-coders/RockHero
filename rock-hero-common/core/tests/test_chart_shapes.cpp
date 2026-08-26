#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Sixteen seconds of default 4/4, which is what every case here needs from the map: a stable beat
// axis and a quarter-beat minimum sustain distance (1/16 whole note in a x/4 meter).
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// A note at one grid slot. The ring is stated by every case, because the span's extent IS the
// members' rings and nothing here is incidental to that.
[[nodiscard]] ChartNote noteAt(
    const int beat, const Fraction offset, const int string, const int fret, const Fraction ring)
{
    ChartNote note;
    note.position = GridPosition{.measure = 1, .beat = beat, .offset = offset};
    note.string = string;
    note.fret = fret;
    note.sustain = ring;
    return note;
}

// A silently-held shape member at one grid slot. The fret is stated only by the cases about a
// member nothing sounds anywhere (β); the reported case authors no fret at all.
[[nodiscard]] ChartHoldMarker markerAt(
    const int beat, const Fraction offset, const int string, const std::optional<int> fret)
{
    return ChartHoldMarker{
        .position = GridPosition{.measure = 1, .beat = beat, .offset = offset},
        .string = string,
        .fret = fret,
    };
}

// The derivation as every reader gets it: from the saved stream, the picture it presents, and the
// chart's own hold markers.
[[nodiscard]] ChartShapes deriveFrom(
    const std::vector<ChartNote>& notes, const std::vector<ChartHoldMarker>& markers = {})
{
    const TempoMap tempo_map = makeTempoMap();
    return deriveChartShapes(notes, presentedChartNotes(notes, tempo_map), markers, tempo_map);
}

} // namespace

// Rule 12's core: two or more fretting-hand strings struck together form a posture, consecutive
// strums of the same articulation merge into one span, and the span runs over the strums' own
// rings — trimmed to the minimum sustain distance before whatever closes it (rule 12a).
TEST_CASE("Chart shape derivation merges repeated strums of one posture", "[core][chart]")
{
    // A quarter-note power chord, the same chord again as an eighth half a beat later, then a
    // single note that closes the held posture.
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 2}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 2}),
        noteAt(2, Fraction{1, 2}, 3, 7, Fraction{1, 2}),
    };
    const ChartShapes derived = deriveFrom(notes);

    // One deduplicated posture: the two strums hold identical frets, so they share it.
    REQUIRE(derived.postures.size() == 1);
    // Indexed by string number, so the array is the model's string bound wide whatever this chart
    // plays — a slot no note fills stays empty rather than being trimmed away.
    REQUIRE(derived.postures.front().frets.size() == static_cast<std::size_t>(g_max_chart_strings));
    CHECK(derived.postures.front().frets[0] == std::optional{5});
    CHECK(derived.postures.front().frets[1] == std::optional{7});
    CHECK_FALSE(derived.postures.front().frets[2].has_value());

    // One merged span from the first strum toward the eighth strum's ring end at 1:2+1/2, trimmed
    // to the margin before the closing onset there — even though presentation draws no tail on
    // either strum (both rings are under the kept-sustain bound).
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes.front().sustain == Fraction{5, 4});
    CHECK(derived.shapes.front().posture == 0);
}

// The closing trim floors at the span's last strum, so a box always reaches its final restrike
// even when the closing event crowds nearer than the margin.
TEST_CASE("Chart shape derivation floors a closing trim at the last strum", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 8}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 8}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 8}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 8}),
        noteAt(2, Fraction{1, 8}, 3, 7, Fraction{1, 8}),
    };
    const ChartShapes derived = deriveFrom(notes);

    // The margin alone would end the span at 7/8 of a beat, before the beat-2 restrike it has to
    // cover; the floor keeps it at exactly that restrike.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().sustain == Fraction{1});
}

// A span crowded closer than the margin, with no room left even at its last strum, falls back to
// exact adjacency rather than collapsing to nothing.
TEST_CASE("Chart shape derivation keeps a crowded span at positive length", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 3, Fraction{1, 8}),
        noteAt(1, Fraction{}, 2, 5, Fraction{1, 8}),
        noteAt(1, Fraction{1, 8}, 3, 7, Fraction{1, 8}),
    };
    const ChartShapes derived = deriveFrom(notes);

    // The closing onset lands exactly on the chord's own ring end, inside the margin: the span
    // ends there, exact-adjacent.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().sustain == Fraction{1, 8});
}

// Any articulation difference is a new chord: the same frets played with a different technique
// open a new span, while the posture table still deduplicates by frets alone.
TEST_CASE("Chart shape derivation splits a span on any articulation change", "[core][chart]")
{
    std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 2}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 2}),
        noteAt(2, Fraction{1, 2}, 3, 7, Fraction{1, 2}),
    };
    notes[2].palm_mute = true;
    notes[3].palm_mute = true;
    const ChartShapes derived = deriveFrom(notes);

    // One frets-identical posture (the hand holds the same shape; the technique renders on the
    // notes), but two spans: each trimmed to the margin before the event that closes it.
    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.shapes.size() == 2);
    CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes[0].sustain == Fraction{3, 4});
    CHECK(derived.shapes[0].posture == 0);
    CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
    CHECK(derived.shapes[1].sustain == Fraction{1, 4});
    CHECK(derived.shapes[1].posture == 0);
}

// The derivation runs inside `normalizeChart`, which the load path deliberately runs BEFORE
// `validateChartRules` — so it is handed strings no rule has bounded yet, and it must not let one
// of them decide anything. It does not, because the posture width is the model's own string bound
// rather than a quantity read off the stream: a note on an impossible string takes no part in any
// posture, and the validator then refuses the document a moment later.
TEST_CASE("Chart shape derivation survives an unvalidated string number", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
        noteAt(1, Fraction{}, 1000000, 3, Fraction{1}),
    };
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.postures.front().frets.size() == static_cast<std::size_t>(g_max_chart_strings));
    CHECK(derived.postures.front().frets[0] == std::optional{5});
    CHECK(derived.postures.front().frets[1] == std::optional{7});
    CHECK(derived.shapes.size() == 1);
}

// The one semantic consequence of deriving at READ time rather than in the importer, and the one
// the lifecycle doc names under "Posture and shape derivation": every reader derives from the
// SETTLED stream, where the importer derived before `normalizeChart` ran. So an articulation the
// rules refuse — a connection claim nothing justifies, here — no longer splits a span that the
// chart's own settled notes say is one.
TEST_CASE("Chart shape derivation reads the settled stream", "[core][chart]")
{
    // Two identical strums, the second attacked as a connection on equal frets — which justifies
    // nothing, so the settle sweep records it as the plain pick it plays as.
    std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1}),
    };
    notes[2].attack = NoteAttack::Legato;
    notes[3].attack = NoteAttack::Legato;

    // Unsettled, the claim is an articulation difference like any other: two boxes.
    CHECK(deriveFrom(notes).shapes.size() == 2);

    // Settled — the only form a reader ever holds, since the normalizer runs before anything else
    // sees the chart — the strums are identical and share one span over both their rings.
    Chart chart;
    chart.tuning.strings = {"E2", "A2"};
    chart.notes = notes;
    const TempoMap tempo_map = makeTempoMap();
    CHECK_FALSE(normalizeChart(chart, tempo_map).empty());
    const ChartResolutions resolutions =
        chartResolutions(chart.notes, chart.hold_markers, tempo_map);
    REQUIRE(resolutions.shapes.size() == 1);
    CHECK(resolutions.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(resolutions.shapes.front().sustain == Fraction{2});
}

// Rule 12's other half: a note still ringing across a chord's onset, on a string the strum does
// not re-strike, joins the posture — the hand is still holding it.
TEST_CASE("Chart shape derivation folds a ringing string into the posture", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 3, 7, Fraction{2}),
        noteAt(2, Fraction{}, 1, 3, Fraction{1, 8}),
        noteAt(2, Fraction{}, 2, 5, Fraction{1, 8}),
    };
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.postures.front().frets.size() >= 3);
    CHECK(derived.postures.front().frets[0] == std::optional{3});
    CHECK(derived.postures.front().frets[1] == std::optional{5});
    // The un-struck third string is part of the posture because it is still sounding.
    CHECK(derived.postures.front().frets[2] == std::optional{7});

    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 2});
}

// Tap-only onsets are transparent to the grouping: they neither form a posture nor close a held
// one, so a chord ringing under two-hand tapping keeps its span.
TEST_CASE("Chart shape derivation rings a span through tap-only onsets", "[core][chart]")
{
    const auto chord_with_taps = [](const Fraction ring) {
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 3, ring),
            noteAt(1, Fraction{}, 2, 5, ring),
            noteAt(2, Fraction{}, 5, 12, Fraction{1, 2}),
            noteAt(2, Fraction{1, 2}, 6, 14, Fraction{1, 2}),
        };
        notes[2].attack = NoteAttack::Tap;
        notes[3].attack = NoteAttack::Tap;
        return notes;
    };

    SECTION("a held chord's span covers the taps")
    {
        const ChartShapes derived = deriveFrom(chord_with_taps(Fraction{2}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        // The taps neither close nor trim it: the span runs the chord's whole ring.
        CHECK(derived.shapes.front().sustain == Fraction{2});
    }

    SECTION("a short-ringing chord's span still ends at its own ring, before the taps")
    {
        const ChartShapes derived = deriveFrom(chord_with_taps(Fraction{1}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1});
    }

    SECTION("one fretting-hand note under simultaneous taps derives no chord")
    {
        // The two-hand-tapping staple: a left-hand tap struck together with two right-hand taps.
        // The taps are invisible, so the onset counts one fretting-hand member — an ordinary
        // single onset, no posture, no span.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 9, Fraction{1, 8}),
            noteAt(1, Fraction{}, 5, 12, Fraction{1, 8}),
            noteAt(1, Fraction{}, 6, 14, Fraction{1, 8}),
            noteAt(1, Fraction{1, 8}, 3, 11, Fraction{1, 8}),
        };
        notes[0].attack = NoteAttack::LeftTap;
        notes[1].attack = NoteAttack::Tap;
        notes[2].attack = NoteAttack::Tap;
        const ChartShapes derived = deriveFrom(notes);
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
    }
}

// The authored half of the posture. A hold marker states the stop a silently-held member takes,
// which no note stream can carry — and nothing else: it never opens a span, never counts toward
// the two-string threshold, and says nothing at all where no span covers it.
TEST_CASE("Chart shape derivation folds a hold marker into the posture", "[core][chart]")
{
    // A two-string strum ringing a whole beat: the span every section below attaches a marker to.
    const std::vector<ChartNote> strum{
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
    };

    SECTION("a fret-carrying marker joins the shape from its start")
    {
        const ChartShapes derived = deriveFrom(strum, {markerAt(1, Fraction{}, 3, 9)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        // Nothing sounds on string 3 anywhere in this chart, which is what makes the authored fret
        // the irreducible residue rather than a second copy of something the notes say.
        CHECK(derived.postures.front().frets[2] == std::optional{9});
        // And the span says so, which is what turns the box into the bracket that can print it.
        CHECK(derived.shapes.front().silent_member);
    }

    SECTION("a marker anywhere INSIDE the span joins it, not only one at the start")
    {
        // The finger comes down partway through the held shape, before its string is ever picked.
        // Nothing special-cases the span's first onset, so this needs no rule of its own.
        const std::vector<ChartNote> long_strum{
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
        };
        const ChartShapes derived = deriveFrom(long_strum, {markerAt(2, Fraction{}, 3, 9)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[2] == std::optional{9});
        CHECK(derived.shapes.front().silent_member);
    }

    SECTION("a marker past the span's own end is inert")
    {
        // The chord stops ringing an eighth of a beat in, so the bracket never reaches beat 2. A
        // marker out there would print a stop under a bracket that does not cover it.
        const std::vector<ChartNote> short_strum{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 8}),
        };
        const ChartShapes derived = deriveFrom(short_strum, {markerAt(2, Fraction{}, 3, 9)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK_FALSE(derived.postures.front().frets[2].has_value());
        CHECK_FALSE(derived.shapes.front().silent_member);
    }

    SECTION("a marker on a string the sound already states adds nothing")
    {
        // String 1 is struck at the span start, so the notes carry its fret. The marker is
        // redundant rather than wrong: it changes no posture and makes no silent member, which is
        // what keeps a redundant claim from flipping a fully-strummed box to a bracket.
        const ChartShapes derived = deriveFrom(strum, {markerAt(1, Fraction{1, 2}, 1, 11)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK_FALSE(derived.shapes.front().silent_member);
    }

    SECTION("a marker where no span is open derives nothing at all")
    {
        // Rule 10 is untouched: a shape still needs two SOUNDING fretting-hand members, so a held
        // finger beside a single note is a single note. The marker is not refused anywhere — it
        // simply attaches to nothing, the unjustified connection claim's degrade.
        const std::vector<ChartNote> lone{noteAt(1, Fraction{}, 1, 5, Fraction{1})};
        const ChartShapes derived = deriveFrom(lone, {markerAt(1, Fraction{}, 3, 9)});
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
    }

    SECTION("a fret-absent marker nothing supplies is inert")
    {
        // The relational form: it states a WHEN and expects a later note on its string to state
        // the WHAT. Nothing sounds on string 3 here, so it resolves to nothing and draws nowhere.
        const ChartShapes derived = deriveFrom(strum, {markerAt(1, Fraction{}, 3, std::nullopt)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK_FALSE(derived.postures.front().frets[2].has_value());
        CHECK_FALSE(derived.shapes.front().silent_member);
    }

    SECTION("the reported case: the late member joins the shape it was already held for")
    {
        // The case the whole record was written for. The shape is taken at its onset and its last
        // member is not struck until later — alone. The marker is authored at the shape's start and
        // states no fret, because the late note states it.
        //
        // Both halves are load-bearing and neither is enough alone. Side ruling (ii) is what keeps
        // the span alive across the lone late strike (before it, that onset closed the span at the
        // exact moment the shape was completed, and the marker had nothing in-span to resolve
        // against); the marker is what puts the string in the posture from the START, which no
        // reading of the notes could ever recover.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            noteAt(2, Fraction{}, 3, 9, Fraction{1}),
        };
        const ChartShapes derived = deriveFrom(notes, {markerAt(1, Fraction{}, 3, std::nullopt)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[2] == std::optional{9});
        CHECK(derived.shapes.front().silent_member);
        // The span now covers the strike that completes the shape rather than dying at the margin
        // before it (3/4 of a beat, which is what this derived before the ruling).
        CHECK(derived.shapes.front().sustain == Fraction{2});
    }

    SECTION("an unvalidated string number decides nothing")
    {
        // Same posture as the note walk keeps: the derivation runs before validation has refused
        // an impossible string, so one cannot reach an array index.
        const ChartShapes derived = deriveFrom(strum, {markerAt(1, Fraction{}, 1000000, 9)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(
            derived.postures.front().frets.size() == static_cast<std::size_t>(g_max_chart_strings));
        CHECK_FALSE(derived.shapes.front().silent_member);
    }
}

// Side ruling (ii): a lone re-pick of a string the open span already holds keeps the span, because
// the hand has demonstrably not left the shape. This is the one-note-at-a-time broken chord over a
// held shape, and it is DERIVED — nothing here is authored.
TEST_CASE("Chart shape derivation rides a span through a lone re-pick", "[core][chart]")
{
    // A two-string chord ringing two whole beats, then one of its own members picked again alone.
    const auto chord_then_repick = [](const Fraction chord_ring) {
        return std::vector<ChartNote>{
            noteAt(1, Fraction{}, 1, 5, chord_ring),
            noteAt(1, Fraction{}, 2, 7, chord_ring),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
        };
    };

    SECTION("the span extends over the re-pick instead of dying at it")
    {
        const ChartShapes derived = deriveFrom(chord_then_repick(Fraction{2}));
        REQUIRE(derived.shapes.size() == 1);
        // Before the ruling the lone onset closed the span at the margin before it: 3/4 of a beat.
        CHECK(derived.shapes.front().sustain == Fraction{2});
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    }

    SECTION("nothing else ringing means the hand HAS left the shape, so the span closes")
    {
        // The witness condition, and the reason this is not "a lone note may always continue a
        // span": with the chord long silent by beat 2, the re-pick is just a note.
        const ChartShapes derived = deriveFrom(chord_then_repick(Fraction{1, 8}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1, 8});
    }

    SECTION("a re-pick with a different articulation closes the span")
    {
        // Rule 11 splits a chord on any articulation change; a lone re-pick is that same question
        // asked of one string, so a palm-muted re-pick of a ringing member is a different gesture.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].palm_mute = true;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{3, 4});
    }

    SECTION("a lone strike on a string the shape never held closes the span")
    {
        // The clean boundary between the derivable and the authored: this is the case only a hold
        // marker can join, which is why the ruling does not swallow it.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].string = 3;
        notes[2].fret = 9;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{3, 4});
    }

    SECTION("a re-pick at the stop a claim states joins the span")
    {
        // The marker branch of condition 1, and the half the sound cannot answer: string 3 is a
        // member only because the chart HOLDS it, so the authored claim is the whole test — and
        // this re-pick is at the stop the claim names.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].string = 3;
        notes[2].fret = 5;
        const ChartShapes derived = deriveFrom(notes, {markerAt(1, Fraction{}, 3, 5)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{2});
        CHECK(derived.postures.front().frets[2] == std::optional{5});
    }

    SECTION("a re-pick at a different stop than the claim states closes the span")
    {
        // The claim is the whole test, which means ALL of it. A claim carrying a fret says where
        // the finger IS, so a re-pick somewhere else is a different hand and splits — exactly as a
        // changed articulation splits the sound branch above. Letting it through would print the
        // claim's stop at the span start while the note inside that same bracket sounds another.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].string = 3;
        notes[2].fret = 9;
        const ChartShapes derived = deriveFrom(notes, {markerAt(1, Fraction{}, 3, 5)});
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{3, 4});
        // The stop the charter authored still prints inside the span it was authored in; what it
        // no longer does is swallow the note that contradicts it.
        CHECK(derived.postures.front().frets[2] == std::optional{5});
    }

    SECTION("a re-pick cannot OPEN a shape")
    {
        // Rule 10 is untouched: two sounding fretting-hand members or no posture at all.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
        };
        const ChartShapes derived = deriveFrom(notes);
        CHECK(derived.shapes.empty());
    }
}

// The whole chain the display gate reads: a silently-held member reaches the arrival rule and
// flips the span, because the bracket is the only mark that can print a fret nothing struck.
TEST_CASE("Chart shape arrival brackets a silently-held member", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3"};
    chart.notes = {
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
    };

    // Without the marker this is one full strum of everything the posture holds: a chord box.
    const ChartResolutions box = chartResolutions(chart.notes, chart.hold_markers, tempo_map);
    REQUIRE(box.shapes.size() == 1);
    CHECK_FALSE(
        chartShapeArrivals(box.presented_notes, box.shapes, box.postures, tempo_map).front());

    chart.hold_markers = {markerAt(1, Fraction{}, 3, 9)};
    const ChartResolutions bracketed = chartResolutions(chart.notes, chart.hold_markers, tempo_map);
    REQUIRE(bracketed.shapes.size() == 1);
    CHECK(chartShapeArrivals(
              bracketed.presented_notes, bracketed.shapes, bracketed.postures, tempo_map)
              .front());
}

// Two different postures each get their own entry, and a stream with nothing struck together
// derives nothing at all.
TEST_CASE("Chart shape derivation tables one posture per distinct fret vector", "[core][chart]")
{
    SECTION("distinct fret vectors are distinct postures")
    {
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 3, Fraction{1, 8}),
            noteAt(1, Fraction{}, 2, 5, Fraction{1, 8}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 8}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 8}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.postures.size() == 2);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].posture == 0);
        CHECK(derived.shapes[1].posture == 1);
    }

    SECTION("a melody line strikes nothing together, so it derives nothing")
    {
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 3, Fraction{1, 8}),
            noteAt(2, Fraction{}, 2, 5, Fraction{1, 8}),
            noteAt(3, Fraction{}, 3, 7, Fraction{1, 8}),
        };
        const ChartShapes derived = deriveFrom(notes);
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
    }
}

} // namespace rock_hero::common::core
