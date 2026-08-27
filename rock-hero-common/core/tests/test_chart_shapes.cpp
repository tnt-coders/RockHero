#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
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

// A silently-held shape member at one grid slot: a note with no onset at all, which is the whole
// of how the chart states a stop the hand takes without sounding it. No ring, because a silent
// hold has none of its own.
[[nodiscard]] ChartNote holdAt(
    const int beat, const Fraction offset, const int string, const int fret)
{
    ChartNote note = noteAt(beat, offset, string, fret, Fraction{});
    note.attack = NoteAttack::None;
    return note;
}

// A right-hand tap at one grid slot: the picking hand articulating a shape the fretting hand is
// holding.
[[nodiscard]] ChartNote tapAt(
    const int beat, const Fraction offset, const int string, const int fret, const Fraction ring)
{
    ChartNote note = noteAt(beat, offset, string, fret, ring);
    note.attack = NoteAttack::Tap;
    return note;
}

// The same tap, CARRYING the stop the fretting hand holds under it. One record, two facts at one
// slot — the shape a hold and a tap on the same string could never take, since the stream holds a
// slot at most once.
[[nodiscard]] ChartNote tapHoldingAt(
    const int beat, const Fraction offset, const int string, const int fret, const Fraction ring,
    const int held)
{
    ChartNote note = tapAt(beat, offset, string, fret, ring);
    note.held = held;
    return note;
}

// The span a note's CLAIMED stop joined, read by slot the way \ref spanOfHold reads a hold's. The
// note must be one that claims a stop, so a case asserting about a claim that is not there cannot
// pass for the wrong reason.
[[nodiscard]] std::optional<std::size_t> spanOfClaim(
    const std::vector<ChartNote>& stream, const ChartShapes& derived, const int beat,
    const int string)
{
    std::optional<std::size_t> span;
    bool found = false;
    for (std::size_t index = 0; index < stream.size() && !found; ++index)
    {
        if (stream[index].position.beat == beat && stream[index].string == string)
        {
            REQUIRE(claimedStop(stream[index]).has_value());
            span = derived.claim_shapes[index];
            found = true;
        }
    }
    REQUIRE(found);
    return span;
}

// One stream in the chart's own slot order. The cases below list their sounding notes and their
// held stops in whatever order reads best; this is what makes them a legal chart, so no case has
// to interleave two kinds of member by hand.
[[nodiscard]] std::vector<ChartNote> streamOf(std::vector<ChartNote> notes)
{
    std::ranges::sort(notes, chartNoteOrderLess);
    return notes;
}

// The derivation as every reader gets it: from the saved stream and the picture it presents.
[[nodiscard]] ChartShapes deriveFrom(const std::vector<ChartNote>& notes)
{
    const TempoMap tempo_map = makeTempoMap();
    return deriveChartShapes(notes, presentedChartNotes(notes, tempo_map), tempo_map);
}

// The span a silently-held stop joined, read the way a surface reads it: by the hold's own index
// in the stream the derivation was given. The slot must name a hold in that stream — a case
// asserting about a hold that is not there would pass for the wrong reason.
[[nodiscard]] std::optional<std::size_t> spanOfHold(
    const std::vector<ChartNote>& stream, const ChartShapes& derived, const int beat,
    const int string)
{
    std::optional<std::size_t> span;
    bool found = false;
    for (std::size_t index = 0; index < stream.size() && !found; ++index)
    {
        if (stream[index].position.beat == beat && stream[index].string == string)
        {
            REQUIRE(silentHold(stream[index].attack));
            span = derived.claim_shapes[index];
            found = true;
        }
    }
    REQUIRE(found);
    return span;
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
    const ChartResolutions resolutions = chartResolutions(chart.notes, tempo_map);
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

// The authored half of the posture. A silent hold states the stop a held member takes,
// which no note stream can carry, and says nothing at all where no span covers it. It is a MEMBER
// of the shape rather than a strike on it, which the case below this one is about.
TEST_CASE("Chart shape derivation folds a silent hold into the posture", "[core][chart]")
{
    // A two-string strum ringing a whole beat: the span every section below attaches a hold to.
    const std::vector<ChartNote> strum{
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
    };

    SECTION("a held stop joins the shape from its start")
    {
        const std::vector<ChartNote> stream =
            streamOf({strum[0], strum[1], holdAt(1, Fraction{}, 3, 9)});
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        // Nothing sounds on string 3 anywhere in this chart, which is what makes the authored stop
        // the irreducible residue rather than a second copy of something the notes say.
        CHECK(derived.postures.front().frets[2] == std::optional{9});
        // And the span says so, which is what turns the box into the bracket that can print it.
        CHECK(derived.shapes.front().silent_member);
        CHECK(spanOfHold(stream, derived, 1, 3) == std::optional<std::size_t>{0});
    }

    SECTION("a held stop INSIDE the span states the grown shape from its own instant")
    {
        // GROWTH (user ruling 2026-08-27). The finger comes down partway through the held shape,
        // on a string the shape does not state — so from that instant the hand is in a DIFFERENT
        // shape, and the span splits exactly as a strum growing by a string splits. The two spans
        // divide the chord's own ring between them, and the second inherits the strings the first
        // stated, because the hand never left them.
        //
        // Where the charter wants the stop stated from the START, they author it at the start:
        // that is the case the whole record exists for, and it is the section above this one.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            holdAt(2, Fraction{}, 3, 9),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.postures.size() == 2);
        // The strum's own shape, ending exactly where the finger lands.
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{1});
        CHECK_FALSE(derived.shapes.front().silent_member);
        CHECK_FALSE(derived.postures.front().frets[2].has_value());
        // The grown shape, from that instant to where the chord was already ringing to.
        CHECK(derived.shapes.back().position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes.back().sustain == Fraction{1});
        CHECK(derived.shapes.back().silent_member);
        CHECK(derived.postures.back().frets[0] == std::optional{5});
        CHECK(derived.postures.back().frets[1] == std::optional{7});
        CHECK(derived.postures.back().frets[2] == std::optional{9});
        CHECK(spanOfHold(stream, derived, 2, 3) == std::optional<std::size_t>{1});
    }

    SECTION("a held stop on a string the shape already states does not split it")
    {
        // The other half of the growth rule, and its discrimination: this finger is on a string the
        // sound already states, so it takes no NEW stop and states no new shape. The span is one,
        // and the restatement adds nothing to it — which is what the settle then removes.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            holdAt(2, Fraction{}, 2, 7),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{2});
        CHECK_FALSE(derived.shapes.front().silent_member);
        CHECK_FALSE(spanOfHold(stream, derived, 2, 2).has_value());
    }

    SECTION("a held stop past the span's own end is inert")
    {
        // The chord stops ringing an eighth of a beat in, so the bracket never reaches beat 2. A
        // hold out there would print a stop under a bracket that does not cover it.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 8}),
            holdAt(2, Fraction{}, 3, 9),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK_FALSE(derived.postures.front().frets[2].has_value());
        CHECK_FALSE(derived.shapes.front().silent_member);
        CHECK_FALSE(spanOfHold(stream, derived, 2, 3).has_value());
    }

    SECTION("a held stop on a string the sound already states adds nothing")
    {
        // String 1 is struck at the span start, so the notes carry its fret. The hold is redundant
        // rather than wrong: it changes no posture and makes no silent member, which is what keeps
        // a redundant claim from flipping a fully-strummed box to a bracket.
        const std::vector<ChartNote> stream =
            streamOf({strum[0], strum[1], holdAt(1, Fraction{1, 2}, 1, 11)});
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK_FALSE(derived.shapes.front().silent_member);
    }

    SECTION("a lone held stop beside nothing at all derives nothing")
    {
        // One member is no shape, whichever kind it is. The hold is not refused anywhere — it
        // simply attaches to nothing, the unjustified connection claim's degrade.
        const std::vector<ChartNote> stream{holdAt(1, Fraction{}, 3, 9)};
        const ChartShapes derived = deriveFrom(stream);
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
        REQUIRE(derived.claim_shapes.size() == 1);
        CHECK_FALSE(derived.claim_shapes.front().has_value());
    }

    SECTION("the reported case: the late member joins the shape it was already held for")
    {
        // The case the whole record was written for. The shape is taken at its onset and its last
        // member is not struck until later — alone. The hold is authored at the shape's start.
        //
        // Both halves are load-bearing and neither is enough alone. Side ruling (ii) is what keeps
        // the span alive across the lone late strike (before it, that onset closed the span at the
        // exact moment the shape was completed); the hold is what puts the string in the posture
        // from the START, which no reading of the notes could ever recover.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            holdAt(1, Fraction{}, 3, 9),
            noteAt(2, Fraction{}, 3, 9, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
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
        const std::vector<ChartNote> stream =
            streamOf({strum[0], strum[1], holdAt(1, Fraction{}, 1000000, 9)});
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(
            derived.postures.front().frets.size() == static_cast<std::size_t>(g_max_chart_strings));
        CHECK_FALSE(derived.shapes.front().silent_member);
    }
}

// Rule 10 as re-ruled 2026-08-27: a span opens at a slot holding two or more MEMBERS, and a member
// is a sounding fretting-hand onset there OR a silently-held stop there. The rule this replaces
// counted only sounds, which made converting one member of a two-note chord into a held finger
// derive nothing at all — the remaining note was suddenly "lone", the shape evaporated, and the
// fret the conversion had just stored vanished from every surface.
TEST_CASE("Chart shape derivation opens a span on two members of any kind", "[core][chart]")
{
    SECTION("one sound plus one held finger opens a span")
    {
        // The reported case, stated at its smallest: a two-note chord with one member converted.
        const std::vector<ChartNote> stream =
            streamOf({noteAt(1, Fraction{}, 1, 5, Fraction{1}), holdAt(1, Fraction{}, 2, 7)});
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        CHECK(derived.shapes.front().silent_member);
        // The span runs as far as the one member that rings.
        CHECK(derived.shapes.front().sustain == Fraction{1});
        CHECK(spanOfHold(stream, derived, 1, 2) == std::optional<std::size_t>{0});
    }

    SECTION("a lone sound still opens nothing")
    {
        // The half of the old rule that was right, and the discrimination for the section above:
        // it is the HOLD that supplies the second member, not the relaxation of the threshold.
        const ChartShapes derived = deriveFrom({noteAt(1, Fraction{}, 1, 5, Fraction{1})});
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
    }

    SECTION("held fingers added inside a shape state the grown shape, and re-merge on equalization")
    {
        // Two fingers coming down inside a held shape are the same growth as one, and a strum of
        // the shape the span already holds then MERGES into it by span identity — the grown span's
        // articulation is the strings the first span stated, so an identical strum equalizes with
        // it and rule 11's merge does the rest, with nothing added for the hold case.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            holdAt(2, Fraction{}, 3, 9),
            holdAt(2, Fraction{}, 4, 10),
            noteAt(3, Fraction{}, 1, 5, Fraction{2}),
            noteAt(3, Fraction{}, 2, 7, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.postures.size() == 2);
        CHECK(derived.shapes.front().sustain == Fraction{1});
        // One grown span covering both the fingers and the re-strum, not a third shape at beat 3.
        CHECK(derived.shapes.back().position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes.back().sustain == Fraction{3});
        CHECK(derived.postures.back().frets[2] == std::optional{9});
        CHECK(derived.postures.back().frets[3] == std::optional{10});
    }

    SECTION("a strum of a DIFFERENT shape does not merge into the grown one")
    {
        // The negative control for the merge above: equalization is the whole test, so a re-strum
        // that states another articulation opens its own span like any other change.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            holdAt(2, Fraction{}, 3, 9),
            holdAt(2, Fraction{}, 4, 10),
            noteAt(3, Fraction{}, 1, 5, Fraction{2}),
            noteAt(3, Fraction{}, 2, 8, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(stream);
        CHECK(derived.shapes.size() == 3);
    }

    SECTION("each held stop publishes the span it actually joined")
    {
        // Two shapes, one hold inside each. The surfaces place a hold's face at ITS span's start,
        // so the mapping has to name the span rather than merely say that one exists.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            holdAt(1, Fraction{}, 3, 9),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 2}),
            holdAt(3, Fraction{}, 4, 12),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(spanOfHold(stream, derived, 1, 3) == std::optional<std::size_t>{0});
        CHECK(spanOfHold(stream, derived, 3, 4) == std::optional<std::size_t>{1});
    }
}

// The span law for a shape the HAND alone states (user ruling 2026-08-27). Such a span is authored
// in FRONT of the content it describes, so it waits, unended, until something arrives to justify
// it — a fretting-hand note matching one of its claims, or a picking-hand onset on one of its
// posture strings. One that closes with nothing having arrived is evidence of nothing and
// dissolves, exactly as a lone member states nothing.
TEST_CASE("Chart shape derivation justifies a shape the hand alone states", "[core][chart]")
{
    SECTION("held fingers with nothing to justify them dissolve")
    {
        // Two members, so rule 10 opens a span — and nothing ever arrives, so the passage the hand
        // was stated in front of does not exist. Printing a posture over that silence is what this
        // refuses.
        //
        // Deliberately NOT at beat one: a span's length is the difference between its end and its
        // start, so one at global beat zero would read the same however the walk seeded its end.
        const std::vector<ChartNote> stream =
            streamOf({holdAt(3, Fraction{}, 1, 5), holdAt(3, Fraction{}, 2, 7)});
        const ChartShapes derived = deriveFrom(stream);
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
        CHECK(derived.claim_shapes == std::vector<std::optional<std::size_t>>{{}, {}});
    }

    SECTION("a matching note arriving later justifies the shape, and its ring gives the extent")
    {
        // The content the hold fronts: a note on a claimed string at the stop that claim names.
        // Distance is not a window — nothing closed the span in between, so the hand is still
        // stated — and from the arrival the ordinary member-ring rule takes over.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            noteAt(3, Fraction{}, 2, 7, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        CHECK(derived.shapes.front().silent_member);
        // Start to the arriving note's own ring end: two beats of waiting plus its one beat.
        CHECK(derived.shapes.front().sustain == Fraction{3});
    }

    SECTION("a note at a stop no claim states justifies nothing")
    {
        // The discrimination for the section above: it is the fret MATCH that justifies, the same
        // test side ruling (ii) applies to a re-pick, so a different hand ends the statement
        // instead of confirming it.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            noteAt(3, Fraction{}, 2, 9, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        CHECK(derived.shapes.empty());
    }

    SECTION("taps on the posture's own strings justify the shape and carry its extent")
    {
        // The held-shape-under-tapping figure with the holding STATED rather than inferred: the
        // taps sound on strings the hand claims, and their rings are the only evidence of how long
        // the hand is down.
        //
        // The tap is a quantum after the stops, and it HAS to be: a tap on a claimed string at the
        // claim's own instant would put two notes on one (position, string), which is the case the
        // record flags as unrepresentable. So the figure that justifies is the one the charter can
        // actually write, and the same-instant tap that IS writable lands on a string the shape
        // does not hold — the section below this one.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(1, Fraction{1, 8}, 1, 12, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        CHECK(derived.shapes.front().silent_member);
        // The stops' own instant to the tap's ring end: an eighth of a beat of waiting, then two.
        CHECK(derived.shapes.front().sustain == Fraction{17, 8});
    }

    SECTION("a run of taps carries the coverage on, each landing where the last one stopped")
    {
        // Coverage is the union of the attached rings, and the same-string bound clamps a run of
        // taps to exact adjacency — the next onset lands ON the previous ring's end, never inside
        // it. So the frontier test has to be closed at that boundary: asking "is the shape still
        // SOUNDING here" instead would end the coverage at the first tap and leave the rest of the
        // passage outside the shape it articulates.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(2, Fraction{}, 1, 12, Fraction{1}),
            tapAt(3, Fraction{}, 1, 12, Fraction{1}),
            tapAt(4, Fraction{}, 1, 12, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        // The stops' own instant to the last tap's ring end: three beats of taps after one of
        // waiting.
        CHECK(derived.shapes.front().sustain == Fraction{4});
    }

    SECTION("a tap past the coverage does not revive the shape")
    {
        // The discrimination for the section above, and the resurrection side ruling (ii)'s third
        // condition refuses for a span with sound in it: the taps stopped an eighth in, so the
        // hand is demonstrably off the shape long before this one. Attaching it would print the
        // posture over three beats of silence nothing states.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(1, Fraction{1, 8}, 1, 12, Fraction{1, 8}),
            tapAt(4, Fraction{}, 1, 12, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1, 4});
    }

    SECTION("a tap on a string the shape does not hold justifies nothing")
    {
        // The discrimination for the section above: a tap somewhere else is not this shape being
        // articulated, so it leaves the statement unanswered and the span dissolves with it.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(1, Fraction{}, 4, 12, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(stream);
        CHECK(derived.shapes.empty());
    }

    SECTION("a chord at a stop no claim states ends the statement, and the shape dissolves")
    {
        // Nothing about the chord answers the hand's claim, and a chord always opens its own span,
        // so the statement closes unjustified.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            noteAt(3, Fraction{}, 3, 3, Fraction{1}),
            noteAt(3, Fraction{}, 4, 3, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 3});
        CHECK_FALSE(derived.shapes.front().silent_member);
    }

    SECTION("a chord carrying a claimed stop justifies the statement it fronts")
    {
        // The discrimination for the section above, and the case the fret-match law would miss if
        // it were asked only of a LONE re-pick: this chord sounds string one at exactly the stop
        // the hand claimed, so it IS the content the statement was authored in front of. It
        // justifies the shape and then opens its own — growth by a new string keeps splitting —
        // which leaves the held shape stating its posture at its own instant, where the bracket
        // that prints it draws. Routing justification through the lone re-pick alone would instead
        // dissolve the statement and take both authored stops off every surface unseen.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            noteAt(3, Fraction{}, 1, 5, Fraction{1}),
            noteAt(3, Fraction{}, 3, 3, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{});
        CHECK(derived.shapes.front().silent_member);
        REQUIRE(derived.postures.size() == 2);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        CHECK(spanOfHold(stream, derived, 1, 1) == std::optional<std::size_t>{0});
        CHECK(spanOfHold(stream, derived, 1, 2) == std::optional<std::size_t>{0});
        // And the chord is its own shape, unchanged by having answered.
        CHECK(derived.shapes.back().position == GridPosition{.measure = 1, .beat = 3});
        CHECK_FALSE(derived.shapes.back().silent_member);
    }

    SECTION("a finger added to a shape the hand alone stated joins it, and its face stays there")
    {
        // A statement the hand alone makes is ONE statement of a shape being taken: it has no sound
        // to date it by, so a finger arriving later joins it rather than splitting a statement that
        // is still waiting for its content. This is also the one case left where a hold's face is
        // NOT at its own slot — the bracket it prints under is the span's start, two beats earlier.
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            holdAt(2, Fraction{}, 3, 9),
            noteAt(3, Fraction{}, 1, 5, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.postures.front().frets[2] == std::optional{9});
        CHECK(spanOfHold(stream, derived, 2, 3) == std::optional<std::size_t>{0});
    }

    SECTION("held fingers past a shape's ring state the NEXT shape, not a rejoin of the old one")
    {
        // A span stays OPEN across the silence after its members stop ringing, because rule 11
        // lets a later identical strum rejoin it — so asking the walk's cursor instead of the ring
        // would attach these fingers to a span that ended beats ago and go inert there. The strum
        // rings half a beat; the fingers land two beats later, and a matching note justifies them.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            holdAt(3, Fraction{}, 3, 9),
            holdAt(3, Fraction{}, 4, 10),
            noteAt(4, Fraction{}, 4, 10, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 2);
        // The strum's own span is unchanged: it still ends where its members stopped ringing.
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});
        CHECK_FALSE(derived.shapes.front().silent_member);
        // And the held fingers state their own, at their own slot.
        CHECK(derived.shapes.back().position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes.back().silent_member);
        REQUIRE(derived.postures.size() == 2);
        CHECK(derived.postures.back().frets[2] == std::optional{9});
        CHECK(derived.postures.back().frets[3] == std::optional{10});
        CHECK(spanOfHold(stream, derived, 3, 3) == std::optional<std::size_t>{1});
        CHECK(spanOfHold(stream, derived, 3, 4) == std::optional<std::size_t>{1});
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
        // The silent-hold branch of condition 1, and the half the sound cannot answer: string 3
        // is a member only because the chart HOLDS it, so the authored claim is the whole test —
        // and this re-pick is at the stop the claim names.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].string = 3;
        notes[2].fret = 5;
        notes.push_back(holdAt(1, Fraction{}, 3, 5));
        const ChartShapes derived = deriveFrom(streamOf(notes));
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
        notes.push_back(holdAt(1, Fraction{}, 3, 5));
        const ChartShapes derived = deriveFrom(streamOf(notes));
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

// The settle that turns "every held stop states something" from a hope into an invariant
// (\ref sweepInertClaimedStops, declared beside the legato settle it is the sibling of). Three ways
// to state nothing, one test for all of them, because the derivation answers all three alike.
TEST_CASE("The inert-hold settle removes every stop that states nothing", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const auto sweep = [&tempo_map](std::vector<ChartNote> notes) {
        const std::size_t removed = sweepInertClaimedStops(notes, tempo_map).size();
        return std::pair{std::move(notes), removed};
    };

    SECTION("a lone stop states nothing and goes")
    {
        const auto [swept, removed] = sweep({holdAt(1, Fraction{}, 3, 9)});
        CHECK(removed == 1);
        CHECK(swept.empty());
    }

    SECTION("a shape the hand alone stated, with nothing to justify it, goes whole")
    {
        const auto [swept, removed] =
            sweep(streamOf({holdAt(3, Fraction{}, 1, 5), holdAt(3, Fraction{}, 2, 7)}));
        CHECK(removed == 2);
        CHECK(swept.empty());
    }

    SECTION("a stop past its span's own end goes")
    {
        const auto [swept, removed] = sweep(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 8}),
            holdAt(2, Fraction{}, 3, 9),
        }));
        CHECK(removed == 1);
        CHECK(swept.size() == 2);
    }

    SECTION("a stop restating what the shape already says goes")
    {
        // The redundant restatement: string 2 is stated by the sound, so this claim adds no fret,
        // flips no bracket and draws nowhere — the same nothing the two cases above state.
        const auto [swept, removed] = sweep(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            holdAt(2, Fraction{}, 2, 7),
        }));
        CHECK(removed == 1);
        CHECK(swept.size() == 2);
    }

    SECTION("a stop that reaches a shape stays, and a stream with none is untouched")
    {
        const std::vector<ChartNote> stated =
            streamOf({noteAt(1, Fraction{}, 1, 5, Fraction{1}), holdAt(1, Fraction{}, 2, 7)});
        const auto [swept, removed] = sweep(stated);
        CHECK(removed == 0);
        CHECK(swept == stated);
        const auto [sounding, none] = sweep({noteAt(1, Fraction{}, 1, 5, Fraction{1})});
        CHECK(none == 0);
        CHECK(sounding.size() == 1);
    }

    SECTION("the settle runs to a fixpoint: one removal can strand the next")
    {
        // Two rounds, and the second is not reachable in the first. String 2 rings THROUGH the
        // beat-2 onset, so the stop authored there restates what the ring already says and states
        // nothing — but it is what rule 10 counted as the shape's second member. Take it away and
        // the beat-2 slot is a lone note again, the shape it opened is gone, and the stop at beat 3
        // that had joined that shape is left stating nothing in its turn.
        const auto [swept, removed] = sweep(streamOf({
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{2}),
            holdAt(2, Fraction{}, 2, 7),
            holdAt(3, Fraction{}, 3, 9),
        }));
        CHECK(removed == 2);
        REQUIRE(swept.size() == 2);
        CHECK(std::ranges::none_of(swept, [](const ChartNote& note) {
            return silentHold(note.attack);
        }));
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

    // Without the hold this is one full strum of everything the posture holds: a chord box.
    const ChartResolutions box = chartResolutions(chart.notes, tempo_map);
    REQUIRE(box.shapes.size() == 1);
    CHECK_FALSE(
        chartShapeArrivals(box.presented_notes, box.shapes, box.postures, tempo_map).front());

    chart.notes = streamOf({chart.notes[0], chart.notes[1], holdAt(1, Fraction{}, 3, 9)});
    const ChartResolutions bracketed = chartResolutions(chart.notes, tempo_map);
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

// The held stop's whole reason to exist: a claim on the very string a right-hand onset is sounding
// at the very instant it sounds it, which two records could never state (one slot, one note). The
// figure runs END TO END — the holds state the shape, the tap that belongs to them justifies it,
// and the bracket prints at the instant the hand takes the shape.
TEST_CASE("A tap carrying a held stop justifies the shape it fronts", "[core][chart]")
{
    // Two fingers come down on strings 1 and 2 with nothing sounding, and the same instant carries
    // a tap on string 3 whose fretting hand is stopping fret 5 under it.
    const std::vector<ChartNote> notes = streamOf({
        holdAt(1, Fraction{}, 1, 5),
        holdAt(1, Fraction{}, 2, 7),
        tapHoldingAt(1, Fraction{}, 3, 12, Fraction{2}, 5),
    });
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.shapes.size() == 1);
    const ChartShape& shape = derived.shapes.front();
    CHECK(shape.position.beat == 1);
    CHECK(shape.silent_member);
    // The extent is the content's: nothing here rings but the tap, so the span runs its ring.
    CHECK(shape.sustain == Fraction{2});

    // Every one of the three claims resolves into that one span, the tap's own included — which is
    // the discrimination this case exists for. Before the held stop the same-instant clause was
    // unreachable: a tap on a CLAIMED string would have needed a second note at the claim's slot.
    REQUIRE(derived.claim_shapes.size() == notes.size());
    CHECK(spanOfClaim(notes, derived, 1, 1) == std::optional<std::size_t>{0});
    CHECK(spanOfClaim(notes, derived, 1, 2) == std::optional<std::size_t>{0});
    CHECK(spanOfClaim(notes, derived, 1, 3) == std::optional<std::size_t>{0});

    // The posture states all three stops, and string 3 states the HELD fret rather than the tapped
    // one: what the fretting hand holds is what a posture is.
    REQUIRE(shape.posture < derived.postures.size());
    const std::vector<std::optional<int>>& frets = derived.postures[shape.posture].frets;
    CHECK(frets[0] == std::optional{5});
    CHECK(frets[1] == std::optional{7});
    CHECK(frets[2] == std::optional{5});

    // The discrimination: strip the held stop and the tap justifies nothing on a string the shape
    // does not hold, so the shape it was fronting dissolves — the corner the ruling recorded, and
    // the exact difference the held stop makes.
    const std::vector<ChartNote> without = streamOf({
        holdAt(1, Fraction{}, 1, 5),
        holdAt(1, Fraction{}, 2, 7),
        tapAt(1, Fraction{}, 3, 12, Fraction{2}),
    });
    CHECK(deriveFrom(without).shapes.empty());
}

// Growth, for the authored member and for the held stop alike: a stop the hand takes on a string
// the standing shape does not state is a DIFFERENT shape from that instant, so the span splits.
TEST_CASE("A held stop on a new string splits the standing shape", "[core][chart]")
{
    // A chord on strings 1 and 2 rings for two beats. A beat in, a tap on string 3 states that the
    // fretting hand has taken fret 9 there — a string the chord never held.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{2}),
        noteAt(1, Fraction{}, 2, 7, Fraction{2}),
        tapHoldingAt(2, Fraction{}, 3, 12, Fraction{1}, 9),
    });
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.shapes.size() == 2);
    CHECK(derived.shapes[0].position.beat == 1);
    CHECK(derived.shapes[1].position.beat == 2);
    // The two cover the ring end to end: the first ends where the finger came down.
    CHECK(derived.shapes[0].sustain == Fraction{1});

    // The claim belongs to the span it OPENED, which is where its satellite prints.
    CHECK(spanOfClaim(notes, derived, 2, 3) == std::optional<std::size_t>{1});
    // The grown span inherits the chord's strings and adds the new stop.
    REQUIRE(derived.shapes[1].posture < derived.postures.size());
    const std::vector<std::optional<int>>& grown =
        derived.postures[derived.shapes[1].posture].frets;
    CHECK(grown[0] == std::optional{5});
    CHECK(grown[1] == std::optional{7});
    CHECK(grown[2] == std::optional{9});

    // The discrimination: the SAME tap without a held stop is transparent to the grouping, so the
    // chord keeps one span. The split is the claim's doing, not the tap's.
    const std::vector<ChartNote> without = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{2}),
        noteAt(1, Fraction{}, 2, 7, Fraction{2}),
        tapAt(2, Fraction{}, 3, 12, Fraction{1}),
    });
    CHECK(deriveFrom(without).shapes.size() == 1);
}

// The sweep's one law over both shapes of claim, and the one thing it must NOT do: a held stop
// that states nothing takes the FIELD, never the sound the charter wrote.
TEST_CASE("The inert-claim settle clears a held stop without taking its note", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    SECTION("a held stop reaching no shape is cleared, and its tap stays")
    {
        // A lone tap: one member, so no shape opens and the claim reaches nothing.
        std::vector<ChartNote> notes{tapHoldingAt(1, Fraction{}, 3, 12, Fraction{1}, 5)};
        const std::vector<ChartConversion> swept = sweepInertClaimedStops(notes, tempo_map);
        REQUIRE(swept.size() == 1);
        CHECK(swept.front().repair == ChartRepair::InertHeldStop);
        REQUIRE(notes.size() == 1);
        CHECK_FALSE(notes.front().held.has_value());
        // Everything else about the tap survives, which is the difference from a silent hold: the
        // note IS the claim there, so the record goes with it.
        CHECK(notes.front().attack == NoteAttack::Tap);
        CHECK(notes.front().fret == 12);
        CHECK(notes.front().sustain == Fraction{1});
    }

    SECTION("a held stop that states a shape is left alone")
    {
        std::vector<ChartNote> notes = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            tapHoldingAt(1, Fraction{}, 3, 12, Fraction{2}, 5),
        });
        CHECK(sweepInertClaimedStops(notes, tempo_map).empty());
        REQUIRE(notes.size() == 2);
        CHECK(claimedStop(notes[1]) == std::optional{5});
    }
}

// The settle judges the SAVED form, exactly as the editor's plan gate beside it does. A held stop
// left on an attack that cannot carry one is a LATENT, like a scrape's pitched techniques: the
// writer strips it, so it claims nothing, draws nothing, and is not the settle's to read or to
// take. Reading the raw field instead would judge a picture no surface derives.
TEST_CASE("The inert-claim settle judges the saved form, not the latent one", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // Two picked notes at one slot: an ordinary two-string chord, with a held stop left on one of
    // them by an attack change that moved it off a tap.
    ChartNote latent = noteAt(1, Fraction{}, 1, 5, Fraction{2});
    latent.held = 9;
    std::vector<ChartNote> notes = streamOf({latent, noteAt(1, Fraction{}, 2, 7, Fraction{2})});

    // Nothing in the saved form claims a stop at all, so there is nothing here to sweep.
    CHECK(sweepInertClaimedStops(notes, tempo_map).empty());
    REQUIRE(notes.size() == 2);
    CHECK(notes.front().held == std::optional{9});

    // The discrimination, one field apart: the same value on a TAP, where the attack CAN carry it.
    // There it is a real claim on a string nothing sounds, so it resolves into the shape the slot
    // states and the settle leaves it for the opposite reason.
    std::vector<ChartNote> legal = streamOf(
        {tapHoldingAt(1, Fraction{}, 1, 5, Fraction{2}, 9),
         noteAt(1, Fraction{}, 2, 7, Fraction{2})});
    CHECK(sweepInertClaimedStops(legal, tempo_map).empty());
    REQUIRE(legal.size() == 2);
    CHECK(claimedStop(legal.front()) == std::optional{9});
}

} // namespace rock_hero::common::core
