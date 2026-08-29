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

// The same note a measure or more along. `noteAt` states measure 1, which is all every case that
// fits inside one bar has to say; the continuity probes run several bars, because what they pin is
// where a span's extent lands and their answers are whole beats apart.
[[nodiscard]] ChartNote inMeasure(const int measure, ChartNote note)
{
    note.position.measure = measure;
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

// The CLASS both surfaces draw, asked end to end from one stream: the walk's own spans through the
// shared arrival rule, in span order. Derived rather than handed in, because a case that stated its
// own span and posture would be stating the very thing the class is a question about.
[[nodiscard]] std::vector<bool> arpeggiosFrom(const std::vector<ChartNote>& notes)
{
    const TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNote> presented = presentedChartNotes(notes, tempo_map);
    const ChartShapes derived = deriveChartShapes(notes, presented, tempo_map);
    return chartShapeArrivals(presented, derived.shapes, tempo_map);
}

// A note whose fret channel TRAVELS: the same slot note with fret statements added along its ring,
// listed as (offset, fret) pairs so a case reads as the path the hand takes. That path is the whole
// of what [D2]'s two moments are read off, so nothing here is incidental either.
[[nodiscard]] ChartNote travellingAt(
    ChartNote note, const std::vector<std::pair<Fraction, int>>& path)
{
    for (const auto& [offset, fret] : path)
    {
        note.keyframes.push_back(Keyframe{.offset = offset, .fret = fret});
    }
    return note;
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
// even when the closing event crowds nearer than the margin. The floor and the continuity law do
// different jobs and both still run: the law says how far the STATEMENT reached, and the trim then
// shortens that for the event closing it — never past the restrike the box has to cover.
TEST_CASE("Chart shape derivation floors a closing trim at the last strum", "[core][chart]")
{
    // The first strum rings exactly into the second (adjacency), which is what keeps the two in one
    // span at all under the continuity law — a stored gap there would end the statement and this
    // would be two boxes with nothing to floor. Before the law both strums rang an eighth and the
    // case still read as one span, which is the one thing about it that changed.
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1}),
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

// THE CONTINUITY LAW (user ruling 2026-08-27, [D3]): a span's statement is in force while every
// SOUNDING member's STORED ring is continuous — ringing through, or ending exactly at its next
// same-string onset — and the FIRST genuine stored gap on any member ends the span at that ring's
// end. What this replaced is the old maximum-of-the-rings extent, and min-extent is not a second
// rule beside it but this law's box case.
TEST_CASE("Chart shape derivation ends a span at the first stored gap", "[core][chart]")
{
    SECTION("a box with uneven rings ends at the first member to stop")
    {
        // The law's box case. String 2 stops half a beat in with nothing restriking it, which is
        // an authored statement of detachment: the shape stops being stated there, however long
        // string 1 goes on ringing. Its survivor is remainder context that display consumes, and
        // the derivation's answer is the half beat the statement held for. The old maximum rule
        // read the same chart as two whole beats of held shape.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});
    }

    SECTION("adjacency chains a whole run of strums into one span")
    {
        // The strike-into-strike shape a stored chug chain has: every ring ends exactly where the
        // next strum begins, so no member ever gaps and the run is one statement from the first
        // strum through the last strum's ring. This is the extent the law was ruled to KEEP, and
        // it is the same answer the maximum rule gave.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1, 2}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{5, 2});
    }

    SECTION("a stored gap between identical strums opens a second span")
    {
        // The discrimination for the run above, and the merge rule's own half of the law: two
        // strums of one articulation are one span only while the statement between them holds. A
        // gap is a boundary, not a pause — the span no longer outlives its own sound waiting to be
        // rejoined by the next identical strum.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 2}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        // Still one posture: the frets never changed, only how long each statement about them
        // stayed in force.
        CHECK(derived.postures.size() == 1);
    }

    SECTION("a carried ring-through member never bounds the extent")
    {
        // The let-ring texture case, and the rider the law carries: a string ringing ACROSS a
        // chord's onset joins the posture (it is where the hand is) but is EXTENT-INERT, because a
        // texture ringing under a passage must not decide how long the passage's own statements
        // are. Here the carried ring stops a quarter beat into the span and the span runs the full
        // two beats its struck members hold.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 7, Fraction{5, 4}),
            noteAt(2, Fraction{}, 1, 3, Fraction{2}),
            noteAt(2, Fraction{}, 2, 5, Fraction{2}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[2] == std::optional{7});
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes.front().sustain == Fraction{2});
    }
}

// THE CONTINUITY LAW's other half, and the one a right-hand onset must never cross: a sounding
// onset of either hand CONTINUES a chain's statement, but only a MEMBER sounding WRITES its
// length. A tap says nothing about the fretting hand — it joins no posture for exactly that
// reason — so a tap's own ring can never be how far the SHAPE reaches.
//
// Every case here is a tap landing exactly where a member's ring ends, which is the only shape the
// stream can legally take: a tap is a real onset on its own string, so that string's stored ring
// was already clamped to it. Continuity therefore stands at each of these taps — what changed is
// that the chain no longer takes the tap's ring with it, so the member's own ring governs. Each
// section names the answer it would have given while a tap could write a chain.
TEST_CASE("Chart shape derivation never lets a tap write a span's extent", "[core][chart]")
{
    SECTION("a tapped sixteenth does not decide a chord's extent")
    {
        // The chord rings four beats and the tapping hand articulates it at the far end. A chain
        // written by those taps ended the span a sixteenth PAST the chord's own sound, which is a
        // statement about the shape read off the wrong hand entirely.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            inMeasure(2, tapAt(1, Fraction{}, 1, 12, Fraction{1, 16})),
            inMeasure(2, tapAt(1, Fraction{}, 2, 12, Fraction{1, 16})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{4});
    }

    SECTION("a long tap does not carry a span past the fretting hand's last sound")
    {
        // The same figure with the taps ringing on: two beats of tapped sound after the chord's
        // six. A chain written by the taps ran the span to eight — the hand held for six beats and
        // the record claimed eight, which is the lengthening the split forbids.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{6}),
            noteAt(1, Fraction{}, 2, 7, Fraction{6}),
            inMeasure(2, tapAt(3, Fraction{}, 1, 12, Fraction{2})),
            inMeasure(2, tapAt(3, Fraction{}, 2, 12, Fraction{2})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{6});
    }

    SECTION("a tap run does not hold a span past the member's own ring")
    {
        // The corpus's own figure: a chord let ring while the tapping hand runs on one of its
        // strings. String 3's member sound ends at beat 8 and the run picks the string up from
        // there, tap ringing into tap. A chain written by those taps walked the span forward with
        // the run — to eleven, the last tap's ring — while the shape's own sound governs at eight.
        // The two long members are what make the difference visible: with the tapped string's
        // chain the minimum either way, only its VALUE is in question.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{12}),
            noteAt(1, Fraction{}, 2, 7, Fraction{12}),
            noteAt(1, Fraction{}, 3, 9, Fraction{8}),
            inMeasure(3, tapAt(1, Fraction{}, 3, 14, Fraction{1})),
            inMeasure(3, tapAt(2, Fraction{}, 3, 14, Fraction{1})),
            inMeasure(3, tapAt(3, Fraction{}, 3, 14, Fraction{1})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{8});
    }

    SECTION("a plain tap adds nothing to a shape the hand alone stated")
    {
        // The same rule where the extent came from the CLAIM machinery instead of a strum, which
        // is the one place a fronted span gets a length at all: two fingers come down with nothing
        // sounding, the string-3 stop is then PLAYED half a beat long — the arrival that justifies
        // the statement and, being a member, writes its chain — and a plain tap picks the string
        // up exactly where that ring ends. The tap continues the sound, so the statement stays in
        // force across it, and it writes nothing, so the shape still ends where the fretting hand
        // stopped. A chain the tap wrote would have run this bracket two beats further on the
        // strength of the other hand.
        const std::vector<ChartNote> notes = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 3, 5),
            noteAt(2, Fraction{}, 3, 5, Fraction{1, 2}),
            tapAt(2, Fraction{1, 2}, 3, 12, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().silent_member);
        CHECK(derived.shapes.front().sustain == Fraction{3, 2});
    }
}

// The other half of the same law, and the half that decides WHICH ONSETS the chain is continuous
// through (user ruling 2026-08-28, F2): the adjacency set is every SOUNDING onset, whichever hand
// made it. The warrant is what happens to the member's tail at a tap — the tap ends it underneath,
// with no hand lifting anywhere, so the sound was REPLACED rather than silenced, and detachment is
// a statement about sound stopping. A tap therefore chains a statement it may not bound.
//
// This is the case the extent probes above deliberately cannot see: every one of them answers the
// same whether the set holds taps or not, because their taps land where the statement was ending
// anyway. Here the tap sits in the MIDDLE of the figure and a fretting-hand re-pick follows it, so
// the set alone decides whether this is one statement or a shape that died at the first tap.
TEST_CASE("Chart shape derivation chains a statement through a tap on a member", "[core][chart]")
{
    // The two-hand run over a held statement. String 1 states fret 5 and rings a beat into a TAP
    // at a different fret (beat 2, ringing into beat 3) — the tapping hand taking over the string
    // the fretting hand is still holding — and the fretting hand then re-picks its own fret 5 at
    // beat 3, while string 2 rings through the whole figure as a member.
    //
    // The tap's sound bridges: string 1 never goes quiet, so the statement is in force at the
    // re-pick, the re-pick continues it (side ruling (ii)) and writes the member chain that runs
    // to the end. One span over the whole figure, and the tap's own fret 12 is nowhere in the
    // posture — audible to the chain, invisible to the shape.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(1, Fraction{}, 2, 7, Fraction{3}),
        tapAt(2, Fraction{}, 1, 12, Fraction{1}),
        noteAt(3, Fraction{}, 1, 5, Fraction{1}),
    });
    const ChartShapes derived = deriveFrom(notes);
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes.front().sustain == Fraction{3});
    REQUIRE(derived.postures.size() == 1);
    CHECK(derived.postures.front().frets[0] == std::optional{5});
    CHECK(derived.postures.front().frets[1] == std::optional{7});
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

    SECTION("a held stop restating the stop the sound already states adds nothing")
    {
        // String 1 is struck at the span start, so the notes carry its fret, and this finger is on
        // that same fret. The hold is redundant rather than wrong: it changes no posture and makes
        // no silent member, which is what keeps a redundant claim from flipping a fully-strummed
        // box to a bracket. A finger on a DIFFERENT fret would say the hand had MOVED, which splits
        // the span — the case pinned beside the mid-span continue/split law below.
        const std::vector<ChartNote> stream =
            streamOf({strum[0], strum[1], holdAt(1, Fraction{1, 2}, 1, 5)});
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
        // The span runs as far as the one member that rings — and this is also where the
        // continuity law's CLAIM EXEMPTION is pinned: a claim has no ring at all, so if it bounded
        // the extent like a sounding member this span would have to end at its own instant. It
        // states where a finger is, never how long anything sounds.
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
// in FRONT of the content it describes, so it waits, unended, until one of its own HELD frets is
// PLAYED: a sounding fretting-hand note matching one of its claims, which is the only thing that
// justifies it. Right-hand onsets justify nothing, however many of them ring over the shape. One
// that closes with nothing having arrived is evidence of nothing and dissolves, exactly as a lone
// member states nothing.
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

    SECTION("taps justify nothing, on the shape's own strings or anywhere else")
    {
        // The user's rule, 2026-08-27: the span requires one of its HELD frets to be PLAYED. A tap
        // sounds where the tapping finger lands, so however many of them ring over the shape they
        // are evidence about the other hand and leave the statement unanswered.
        //
        // Both placements, because the earlier law separated them and this one does not: a run on
        // a string the hand CLAIMS used to justify the shape and carry its whole extent, and one
        // elsewhere justified nothing. Now neither does, and both spans dissolve.
        const std::vector<ChartNote> on_a_claimed_string = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(2, Fraction{}, 1, 12, Fraction{1}),
            tapAt(3, Fraction{}, 1, 12, Fraction{1}),
            tapAt(4, Fraction{}, 1, 12, Fraction{1}),
        });
        CHECK(deriveFrom(on_a_claimed_string).shapes.empty());

        const std::vector<ChartNote> somewhere_else = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(1, Fraction{}, 4, 12, Fraction{2}),
        });
        CHECK(deriveFrom(somewhere_else).shapes.empty());
    }

    SECTION("the pull-off figure: a tap inside, and the arrival at a claimed stop justifies it")
    {
        // The figure the ruling is written for. The hand comes down on two strings with nothing
        // sounding, the picking hand taps above it — which justifies nothing on its own — and then
        // one of the STATED stops is actually played. That arrival is the content the statement
        // was authored in front of, so the whole figure stands, taps and all.
        //
        // The arrival wears a legato attack because that is the figure's own shape; the derivation
        // reads it only as a sounding fretting-hand onset, which is exactly what makes this the
        // discrimination for the section above rather than a second rule about attacks.
        ChartNote arrival = noteAt(3, Fraction{}, 2, 7, Fraction{1});
        arrival.attack = NoteAttack::Legato;
        const std::vector<ChartNote> stream = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(2, Fraction{}, 3, 12, Fraction{1, 2}),
            arrival,
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().silent_member);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        // Extent THROUGH the arrival: two beats of waiting, then the arrival's own ring. The
        // pre-arrival stretch runs start-to-arrival, and from there the ordinary member-ring rule
        // takes over with the arrival counted as a member ring.
        CHECK(derived.shapes.front().sustain == Fraction{3});

        // The discrimination: the same figure with the arrival one fret off answers no claim, so
        // nothing was played that the hand claimed and the whole statement dissolves.
        ChartNote wrong_stop = arrival;
        wrong_stop.fret = 8;
        const std::vector<ChartNote> unanswered = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 7),
            tapAt(2, Fraction{}, 3, 12, Fraction{1, 2}),
            wrong_stop,
        });
        CHECK(deriveFrom(unanswered).shapes.empty());
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

// Side ruling (ii), NARROWED by THE CONTINUITY LAW (user ruling 2026-08-27, [D3]): a lone re-pick
// of a string the open span already holds keeps the span while the span's own statement is still
// in force. This is the one-note-at-a-time broken chord over a held shape, and it is DERIVED —
// nothing here is authored.
//
// The narrowing DELETED the rule's third condition, the presented-ring witness, and the sections
// below are what replaced it: an ADJACENT re-pick is continuity itself, and a GAP re-pick arrives
// after the statement has already ended. Two sections name the divergence in each direction, so
// the deletion is tested rather than merely assumed harmless.
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

    SECTION("a re-pick after a stored gap is an ordinary onset, so the span closes")
    {
        // The reason this is not "a lone note may always continue a span": the chord's rings stop
        // an eighth in with nothing restriking them, so the statement ended there and the beat-2
        // re-pick arrives after it. Under rule 11 it is just a note.
        const ChartShapes derived = deriveFrom(chord_then_repick(Fraction{1, 8}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1, 8});
    }

    SECTION("a gap on the re-picked string closes the span even while another member rings")
    {
        // The narrowing's own population, and the first divergence from the deleted witness.
        // String 2 rings loudly through beat 2, so the WITNESS said the hand had not left the
        // shape and continued the span to two whole beats. The law asks the re-picked string's own
        // stored ring instead: string 1 stopped half a beat in, which is an authored detachment,
        // so the statement ended there and this re-pick joins nothing.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[0].sustain = Fraction{1, 2};
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});
    }

    SECTION("an adjacent re-pick continues where no drawn tail could witness it")
    {
        // The divergence in the other direction, and the reason the witness was worse evidence
        // than the stored stream it stood in for: string 2 is a dead click, so E25 takes its tail
        // away in presentation and the witness saw NOTHING ringing — the span died at the margin
        // before this re-pick (3/4 of a beat) although the hand was demonstrably still holding the
        // shape. The stored ring says what the hand did: string 1 rings exactly into its own
        // re-pick, string 2 rings through it, so every member is continuous and the statement
        // stands through both.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
        };
        notes[1].dead = true;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{2});
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
    CHECK_FALSE(chartShapeArrivals(box.presented_notes, box.shapes, tempo_map).front());

    chart.notes = streamOf({chart.notes[0], chart.notes[1], holdAt(1, Fraction{}, 3, 9)});
    const ChartResolutions bracketed = chartResolutions(chart.notes, tempo_map);
    REQUIRE(bracketed.shapes.size() == 1);
    CHECK(chartShapeArrivals(bracketed.presented_notes, bracketed.shapes, tempo_map).front());
}

// LAW III's CLASS rule asked of a span's INTERIOR (user ruling 2026-08-27): ARPEGGIO iff the
// shape's members sound SEPARATELY, so a span stays a box chain only while every sounding of it is
// the shape WHOLE. The two figures the interior arm covers are one fact at two widths — a partial
// restrike and a lone re-pick of a single member — which is why they need no clause each.
//
// Two negatives carry the rule's edges, and both are needed. Rule 11 is unchanged underneath: a
// partial restrike with NO ring behind it is interior to nothing, because it split into a span of
// its own. And an interior slot that restates the shape WHOLE is the box chain the repeat marks are
// for, which is what keeps this from collapsing into "any span with an interior onset".
TEST_CASE("Chart shape arrival brackets a span its members sound in parts", "[core][chart]")
{
    // A three-string shape struck whole, then two of its three struck again a beat later. What the
    // carried ring decides is whether the second strum is INSIDE the first's statement at all.
    const auto strum_then_partial = [](const Fraction carried_ring) {
        return std::vector<ChartNote>{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 9, carried_ring),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
        };
    };

    SECTION("a partial restrike inside a ringing span flips the whole span")
    {
        // String 3 rings THROUGH the restrike, which folds it into that slot's posture and merges
        // the two strums into one statement — so the ring is exactly why the slot is inside the
        // span. Inside it, two of the three members sound: the shape came apart there, and the
        // statement it came apart in is an arpeggio for its whole length.
        const std::vector<ChartNote> notes = strum_then_partial(Fraction{2});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{2});
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());
    }

    SECTION("with no ring behind it the partial restrike splits, and both spans stay boxes")
    {
        // Rule 11 unchanged, and the reason the interior arm can never reach this: string 3's ring
        // ends exactly AT the restrike, so nothing folds in, the articulations differ, and the
        // smaller strum states a shape of its own. Neither span sounds anything but itself whole.
        const std::vector<ChartNote> notes = strum_then_partial(Fraction{1});
        REQUIRE(deriveFrom(notes).shapes.size() == 2);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK_FALSE(arpeggio[1]);
    }

    SECTION("a lone re-pick continuation is the same fact at one string's width")
    {
        // Rule 11's exception rides the span over a single member's re-pick, and that slot sounds
        // ONE of the shape's two members — so every span a lone re-pick continues is an arpeggio by
        // definition, which is the whole of what "one fact at two widths" means.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
        };
        REQUIRE(deriveFrom(notes).shapes.size() == 1);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());
    }

    SECTION("the last strum sitting exactly ON the span end is still inside it")
    {
        // The edge that decides where this rule can LIVE. A different chord crowds in a sixteenth
        // after the re-pick, so rule 12a's trim floors the span at its own last strum and the span
        // ends exactly ON that re-pick. A class re-derived from the trimmed extent cannot tell that
        // slot from the onset that CLOSES a span, which the exact-adjacency fallback puts on the
        // end too — 48 of the corpus's 305 lone-re-pick spans hold their re-pick only there, and 38
        // of those are the closing kind — while the walk needs no such test, because riding the
        // slot is what it did.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{5, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{1, 4}, 1, 3, Fraction{1}),
            noteAt(2, Fraction{1, 4}, 2, 3, Fraction{1}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        // One beat of span from a start on beat one: the end lands on the re-pick, not past it.
        CHECK(derived.shapes.front().sustain == Fraction{1});
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK(arpeggio.front());
    }

    SECTION("a chug chain restating the shape whole stays a box")
    {
        // The interior arm's own discriminating negative: one span, two interior slots, and each of
        // them sounds the shape WHOLE. This is what the repeat boxes are drawn over.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1}),
        };
        REQUIRE(deriveFrom(notes).shapes.size() == 1);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK_FALSE(arpeggio.front());
    }
}

// THE F1 PROBE, permanent (user ruling 2026-08-28): CLASSIFICATION READS THE STORED STREAM.
//
// One figure, two arms of the one class law, and they used to disagree. A DEAD string carries into
// a chord's onset: its STORED ring is real timing, its PRESENTED tail is gone (E25). The walk's
// fold-in has always asked the stored ring, so the carry joined the posture and any interior
// restrike then classified the span as an arpeggio — while the arrival rule re-derived the same
// carry off the PRESENTED tail at the span's start and found nothing there. Same figure, same law,
// two answers, decided by which slot you happened to look at.
//
// The ruling makes the stored reading the only one and the re-derivation is gone, so the two arms
// are the same comparison now. Both sections below assert ONE answer.
TEST_CASE("A dead string's carry classifies at a span start and inside it alike", "[core][chart]")
{
    // String 3 is struck DEAD at beat one and rings three beats in the stored stream; the chord on
    // strings 1 and 2 lands a beat later, inside that ring.
    const auto dead_carry = [](const bool restrike) {
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 9, Fraction{3}),
            noteAt(2, Fraction{}, 1, 5, Fraction{2}),
            noteAt(2, Fraction{}, 2, 7, Fraction{2}),
        };
        notes.front().dead = true;
        if (restrike)
        {
            notes.push_back(noteAt(3, Fraction{}, 1, 5, Fraction{1}));
            notes.push_back(noteAt(3, Fraction{}, 2, 7, Fraction{1}));
        }
        return streamOf(notes);
    };

    SECTION("the START arm: the carry alone brackets the span")
    {
        // Nothing restrikes, so the only slot that says anything is the span's own start — where
        // two of the shape's three sounding strings arrive and the third was already down. This is
        // the arm that answered BOX before the ruling, because the dead note presents no tail.
        const std::vector<ChartNote> notes = dead_carry(false);
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        // The dead string joined the posture, which is what a carry means and what E25 never
        // touched: presentation takes the drawn tail, not the fact that the finger is down.
        REQUIRE(derived.shapes.front().posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes.front().posture].frets[2] == std::optional{9});
        CHECK(derived.shapes.front().sounds_in_parts);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());
    }

    SECTION("the INTERIOR arm agrees, on the same figure")
    {
        // The same carry, plus a restrike of the two struck members a beat later. That slot sounds
        // two of three as well, so the interior arm answers exactly what the start arm just did —
        // one comparison, one span, one answer, whichever slot asks.
        const std::vector<ChartNote> notes = dead_carry(true);
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sounds_in_parts);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());
    }

    SECTION("a start that sounds the shape WHOLE is still a box, dead members and all")
    {
        // The discriminating negative, and the reason the flip is not "dead notes are arpeggios":
        // nothing is CARRIED here. The dead string is struck with the others, so every member of
        // the shape sounds at the one slot and the box stands.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
        };
        notes.back().dead = true;
        const std::vector<ChartNote> stream = streamOf(notes);
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK_FALSE(derived.shapes.front().sounds_in_parts);
        const std::vector<bool> arpeggio = arpeggiosFrom(stream);
        REQUIRE(arpeggio.size() == 1);
        CHECK_FALSE(arpeggio.front());
    }

    SECTION("a thin start still brackets, through the member it holds")
    {
        // What the deleted "fewer than two sounds at the start" clause was a precondition OF. One
        // string struck beside one finger held: rule 10 counts two MEMBERS and opens a span, and
        // the held one has no sound of its own, so the span arrives an arpeggio through its silent
        // member rather than through a clause about how thin the start was.
        const std::vector<ChartNote> notes =
            streamOf({noteAt(1, Fraction{}, 1, 5, Fraction{2}), holdAt(1, Fraction{}, 3, 9)});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().silent_member);
        CHECK_FALSE(derived.shapes.front().sounds_in_parts);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());
    }
}

// [D1]'s mechanism, pinned: what a claim restating a member of the STANDING shape does at the slot
// it is written on — the repeated-chord member the charter converts to a held finger. ONE authority
// answers both halves and no verb special case exists beside it: the ARTICULATION comparison
// decides whether the slot merges or splits, and the close's own posture build then decides
// whether the claim has a face at all — a string the shape already states is skipped, so the claim
// publishes no reach and the settle takes it.
//
// The MEMBER'S OWN RING is what selects between the two, which is the same fact the interior class
// rule reads: a ring that stops at the claim makes the claim a new statement, and a ring that
// crosses it makes the claim a restatement of one already in force.
TEST_CASE("A claim restating a shape's own member merges or splits by the ring", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    // Five strums of one three-string shape, with the third strum's top member converted to a
    // silently-held stop. `carried_ring` is the ring of the member the SECOND strum sounded — the
    // one that either stops at the converted slot or crosses it.
    const auto converted_member = [](const Fraction carried_ring) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 3, 9, carried_ring),
            noteAt(3, Fraction{}, 1, 5, Fraction{1}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1}),
            holdAt(3, Fraction{}, 3, 9),
            noteAt(4, Fraction{}, 1, 5, Fraction{1}),
            noteAt(4, Fraction{}, 2, 7, Fraction{1}),
            noteAt(4, Fraction{}, 3, 9, Fraction{1}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 5, Fraction{1})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 7, Fraction{1})),
            inMeasure(2, noteAt(1, Fraction{}, 3, 9, Fraction{1})),
        });
    };

    SECTION("a ring that stops at the claim SPLITS, and the claim founds the new statement")
    {
        // The sandwich, end to end. The second strum's third member rings only into the converted
        // slot, so nothing folds in there and the articulation comparison differs — C struck
        // against C silent — which is rule 11's ordinary answer and needs no clause of its own. The
        // claim is then a span START claim, which [D1] leaves licensed: it prints its stop in the
        // new span's bracket, and that span arrives an arpeggio because it holds a silent member.
        // The boxes on either side are untouched, which is what makes this a sandwich and not a
        // rewrite of the chain.
        const std::vector<ChartNote> notes = converted_member(Fraction{1});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].silent_member);
        CHECK(spanOfHold(notes, derived, 3, 3) == std::optional<std::size_t>{1});

        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 3);
        CHECK_FALSE(arpeggio[0]);
        CHECK(arpeggio[1]);
        CHECK_FALSE(arpeggio[2]);

        // The claim states a posture nothing else can print, so the settle leaves it alone.
        std::vector<ChartNote> settled = notes;
        CHECK(sweepInertClaimedStops(settled, tempo_map).empty());
    }

    SECTION("a ring that crosses the claim MERGES, and the claim states nothing")
    {
        // [D1]'s refusal, and it needs no mechanism of its own because the shipped close already
        // is one. The second strum's third member rings THROUGH the converted slot, so it folds
        // into that slot's posture, the articulations match and the whole run stays one statement.
        // The claim then lands on a string the shape already states: the close skips it, publishes
        // no reach for it, and the settle takes the record — the mid-chain "still held" claim is
        // unstatable exactly as ruled, with no second redundancy rule and no verb special case.
        //
        // What DOES survive is the truth underneath it: the members sounded separately at that
        // slot, so the interior class rule brackets the whole span. That is the ruling's own
        // reading of this figure — the record for it is the merged ring, never the claim.
        const std::vector<ChartNote> notes = converted_member(Fraction{2});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK_FALSE(derived.shapes.front().silent_member);
        CHECK(spanOfHold(notes, derived, 3, 3) == std::nullopt);

        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());

        std::vector<ChartNote> settled = notes;
        CHECK(sweepInertClaimedStops(settled, tempo_map).size() == 1);
        CHECK(std::ranges::none_of(settled, [](const ChartNote& note) {
            return silentHold(note.attack);
        }));
    }
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
// at the very instant it sounds it, which two records could never state (one slot, one note). What
// that buys is MEMBERSHIP at that instant — the stop counts toward the shape and prints in its
// posture — and the bracket then draws at the instant the hand takes the shape.
// A held-carrying tap is a CLAIM like any other — it counts toward the member threshold and joins
// the span's posture — and it is ALSO the sound of the stop it claims (user ruling 2026-08-27, the
// tap-harmonic arm): the pitch of a harmonic tapped above a stop derives from the stopped length,
// so the record states the stop and plays it in one note. What the earlier law called a
// self-justification wart is therefore the truth of the figure, and this case is its inversion.
TEST_CASE("A tap carrying a held stop plays the stop it claims", "[core][chart]")
{
    SECTION("the single-string figure justifies the shape it claims into")
    {
        // Two fingers come down on strings 1 and 2 with nothing sounding, and the same instant
        // carries a tap on string 3 whose fretting hand is stopping fret 5 under it. Three claims,
        // and one of them is heard: string 3's fret-5 stop sounds through the tap above it, which
        // is one of the shape's own held frets being played and the whole of what the span needs.
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
        // Emitted at its own instant, with no extent: justification is not EXTENT, and a tap
        // extends no span's ring — it is the other hand's onset, transparent to the grouping — so
        // nothing here carries the statement forward. The same length a chord that answers a claim
        // without joining the span leaves behind.
        CHECK(shape.sustain == Fraction{});

        // Every one of the three claims resolves into that one span, the tap's own included.
        REQUIRE(derived.claim_shapes.size() == notes.size());
        CHECK(spanOfClaim(notes, derived, 1, 1) == std::optional<std::size_t>{0});
        CHECK(spanOfClaim(notes, derived, 1, 2) == std::optional<std::size_t>{0});
        CHECK(spanOfClaim(notes, derived, 1, 3) == std::optional<std::size_t>{0});

        // The posture states all three stops, and string 3 states the HELD fret rather than the
        // tapped one: what the fretting hand holds is what a posture is.
        REQUIRE(shape.posture < derived.postures.size());
        const std::vector<std::optional<int>>& frets = derived.postures[shape.posture].frets;
        CHECK(frets[0] == std::optional{5});
        CHECK(frets[1] == std::optional{7});
        CHECK(frets[2] == std::optional{5});

        // And the whole figure survives the settle beside it: every claim reached the span, so
        // there is nothing here that states nothing.
        std::vector<ChartNote> settled = notes;
        CHECK(sweepInertClaimedStops(settled, makeTempoMap()).empty());
    }

    SECTION("a tap over a claimed stop answers it, and one over a different stop does not")
    {
        // The discrimination that keeps random taps out. The hand states fret 5 on strings 1 and 3
        // at beat one; a beat later the picking hand taps above string 3. What decides is the stop
        // UNDER the tap, not the tap: over the claimed stop it is that stop sounding, and the
        // statement stands.
        const auto figure = [](const int held) {
            return streamOf({
                holdAt(1, Fraction{}, 1, 5),
                holdAt(1, Fraction{}, 3, 5),
                tapHoldingAt(2, Fraction{}, 3, 12, Fraction{1}, held),
            });
        };
        const ChartShapes answered = deriveFrom(figure(5));
        REQUIRE(answered.shapes.size() == 1);
        CHECK(answered.shapes.front().position.beat == 1);
        CHECK(answered.shapes.front().silent_member);
        // And answering is not LENGTHENING, even a beat later and even though this tap's own ring
        // is real evidence about the stop (a tapped harmonic dies the moment the held fret lifts).
        // That evidence routes through the CLAIM, and a claim states where a finger is rather than
        // how long anything sounds — so the shape still stands at its own instant, exactly as it
        // does when the answering tap lands in the span's own slot. Only a MEMBER's sound writes
        // an extent, which is the same rule that keeps a tap from bounding a sounding span.
        CHECK(answered.shapes.front().sustain == Fraction{});

        // A tap holding a DIFFERENT fret sounds a stop this shape never claimed, so it answers
        // nothing — and a claim it makes a beat into a span that is still waiting is not one the
        // span states either. Nothing was played that the hand claimed, and the whole statement
        // dissolves.
        CHECK(deriveFrom(figure(9)).shapes.empty());
    }
}

// A claim whose ANSWERING justifies a span has reached that span (user ruling 2026-08-27): take the
// record away and the span dissolves, so it states exactly as much as a member does. That is what
// keeps the settle's two questions — does this record state anything, does it do anything — one
// question with one answer, instead of the sweep growing a rule about justification beside the one
// it already asks.
TEST_CASE("A claim that justifies a shape reaches it", "[core][chart]")
{
    SECTION("the plain held-carrying tap that justifies a waiting shape survives the settle")
    {
        // The figure that used to dissolve. Two fingers come down at beat one with nothing
        // sounding, both on fret 5, and a beat later a PLAIN tap sounds the string-3 stop under it
        // — one of the shape's own held frets played inside the span, which justifies it. The claim
        // that tap MAKES lands past the instant a waiting span states its stops at, so it prints no
        // digit of its own; before the ruling the sweep read that as stating nothing, cleared the
        // field, and the span lost the only evidence it had.
        //
        // Plain deliberately: a tapped harmonic is held in place by the sweep's own rule about the
        // pitch a note speaks from, so a node here would prove nothing about justification.
        std::vector<ChartNote> notes = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 3, 5),
            tapHoldingAt(2, Fraction{}, 3, 12, Fraction{1}, 5),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().silent_member);

        // The answering claim reached the span it justified, which is the whole of the fix.
        CHECK(spanOfClaim(notes, derived, 2, 3) == std::optional<std::size_t>{0});
        // So the settle takes nothing, and the figure still stands after it.
        CHECK(sweepInertClaimedStops(notes, makeTempoMap()).empty());
        REQUIRE(notes.size() == 3);
        CHECK(claimedStop(notes.back()) == std::optional{5});
    }

    SECTION("a redundant claim that justifies nothing still goes")
    {
        // The discrimination, at the same fret value. The chord SOUNDS strings 1 and 2 for two
        // beats, so nothing here waits to be justified, and the tap a beat in restates the very
        // stop string 1 is already sounding. Take that claim away and the derivation says exactly
        // the same thing, which is what inert means — and the settle clears the field while the tap
        // itself stays.
        std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            tapHoldingAt(2, Fraction{}, 1, 12, Fraction{1}, 5),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK_FALSE(derived.shapes.front().silent_member);
        CHECK_FALSE(spanOfClaim(notes, derived, 2, 1).has_value());

        const std::vector<ChartConversion> swept = sweepInertClaimedStops(notes, makeTempoMap());
        REQUIRE(swept.size() == 1);
        CHECK(swept.front().repair == ChartRepair::InertHeldStop);
        REQUIRE(notes.size() == 3);
        CHECK_FALSE(notes.back().held.has_value());
        CHECK(notes.back().attack == NoteAttack::Tap);
    }
}

// The mid-span continue/split law (user ruling 2026-08-27): "If the held fret is the same as the
// span, the span knows to continue. If it is different it would split the span." One law over both
// ways a shape states where a finger is — by sound or by claim — because a moved finger is a
// different shape however the old stop was written down.
TEST_CASE("A held stop inside a shape continues it or splits it, by the fret", "[core][chart]")
{
    // The hand states fret 5 on strings 1 and 3 at beat one with nothing sounding; a note at beat
    // two PLAYS the string-1 stop, which justifies the statement and gives it an extent of two more
    // beats. A tap harmonic lands inside that extent at beat three, and what it does to the span is
    // decided by the stop UNDER it, never by the tap.
    const auto figure = [](const int held) {
        ChartNote harmonic = tapHoldingAt(3, Fraction{}, 3, 17, Fraction{1}, held);
        harmonic.harmonic_node = 17.0;
        return streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 3, 5),
            noteAt(2, Fraction{}, 1, 5, Fraction{2}),
            harmonic,
        });
    };

    SECTION("the stop the shape already states continues it")
    {
        const std::vector<ChartNote> notes = figure(5);
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        // Start to the arriving note's own ring end, unbroken through the tap.
        CHECK(derived.shapes.front().sustain == Fraction{3});
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[2] == std::optional{5});
    }

    SECTION("a different stop splits it, and the grown shape states the fret the hand moved to")
    {
        std::vector<ChartNote> notes = figure(9);
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.postures.size() == 2);
        // The old shape ends where the finger moved, and states the stop it was holding.
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{2});
        CHECK(derived.postures.front().frets[2] == std::optional{5});
        // The new one keeps the string the hand never left and states the stop it moved to — the
        // superseded claim dropped rather than inherited, or the split would print the old fret.
        CHECK(derived.shapes.back().position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.postures.back().frets[0] == std::optional{5});
        CHECK(derived.postures.back().frets[2] == std::optional{9});
        // The claim that split the span states the shape it opened, so it reaches it — and the
        // settle, which would otherwise clear the split's own evidence, takes nothing.
        CHECK(spanOfClaim(notes, derived, 3, 3) == std::optional<std::size_t>{1});
        CHECK(sweepInertClaimedStops(notes, makeTempoMap()).empty());
    }

    SECTION("a silent hold contradicting what the SOUND states splits it too")
    {
        // The same law asked of the other shape of claim, against the other way a shape states a
        // stop: string 1 is sounding fret 5 when a finger is authored at fret 11 on it. That is the
        // hand moving, not a restatement, so the span splits and the grown shape prints where the
        // finger went.
        std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            holdAt(2, Fraction{}, 1, 11),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.postures.size() == 2);
        CHECK(derived.shapes.front().sustain == Fraction{1});
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.shapes.back().position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.postures.back().frets[0] == std::optional{11});
        CHECK(derived.postures.back().frets[1] == std::optional{7});
        CHECK(derived.shapes.back().silent_member);
        CHECK(spanOfHold(notes, derived, 2, 1) == std::optional<std::size_t>{1});
        CHECK(sweepInertClaimedStops(notes, makeTempoMap()).empty());
    }
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

// The SAME law where the slot also SOUNDS. Growth is a statement about the fretting hand, so a
// strum landing under the new finger changes nothing about when that finger came down: gating the
// split on silence would date the stop from the shape's onset — printing its fret a beat before the
// charter wrote it — which is the one thing "one written later says the finger came down later"
// forbids. Each section is one of the three ways a slot can continue a standing span, and each
// carries the control that shows the split is the CLAIM's doing rather than the slot's.
//
// The closing trim is where these differ from the silent-slot case, and it is rule 12a doing its
// ordinary job: something sounds at the split, so the shape being replaced keeps the display margin
// from it (a quarter beat here) instead of ending exactly where the successor starts.
TEST_CASE("A held stop on a new string splits a SOUNDING slot's standing shape", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();

    SECTION("a re-strum of the whole shape carries the new finger without back-dating it")
    {
        // Two identical strums a beat apart — the chain that would otherwise merge into one box —
        // with a finger arriving on a third string under the second one.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            holdAt(2, Fraction{}, 3, 9),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        // The shape that was replaced keeps rule 12a's margin before the strum that replaced it.
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1});

        // The stop prints in the span it opened, which is where the charter wrote it, and the
        // bracket that prints it is what the class rule then has to give it.
        CHECK(derived.shapes[1].silent_member);
        CHECK(spanOfHold(notes, derived, 2, 3) == std::optional<std::size_t>{1});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& grown =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(grown[0] == std::optional{5});
        CHECK(grown[1] == std::optional{7});
        CHECK(grown[2] == std::optional{9});
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK(arpeggio[1]);

        // The claim states a posture nothing else can print, so the settle leaves it alone.
        std::vector<ChartNote> settled = notes;
        CHECK(sweepInertClaimedStops(settled, tempo_map).empty());

        // The control: the same two strums with no finger arriving are one box, so the split is
        // the claim's doing and not the second strum's.
        const std::vector<ChartNote> without = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
        });
        const ChartShapes merged = deriveFrom(without);
        REQUIRE(merged.shapes.size() == 1);
        CHECK(merged.shapes.front().sustain == Fraction{2});
        CHECK_FALSE(arpeggiosFrom(without).front());
    }

    SECTION("a full restrike of a three-string shape answers the same way")
    {
        // The figure the ruling names: every member struck again at the slot the fourth finger
        // lands on. Nothing about the strum being COMPLETE makes the new stop older than its slot.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 3, 9, Fraction{1}),
            holdAt(2, Fraction{}, 4, 11),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes[1].silent_member);
        CHECK(spanOfHold(notes, derived, 2, 4) == std::optional<std::size_t>{1});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[1].posture].frets[3] == std::optional{11});
        // A full restrike is the shape whole, so the successor is NOT sounded in parts by it — its
        // bracket comes from the held member alone.
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
    }

    SECTION("a lone re-pick under the new finger dates it from the re-pick")
    {
        // The third continuation, and the one whose own comment used to read as licence: a re-pick
        // carrying a held finger beside it does not let the MEMBER COUNT open a fresh shape, which
        // is a different question from whether the finger grows the standing one.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            holdAt(2, Fraction{}, 3, 9),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes[1].silent_member);
        CHECK(spanOfHold(notes, derived, 2, 3) == std::optional<std::size_t>{1});
        // The successor inherits the strings the hand never left, so the re-pick is a member of it
        // rather than a shape of its own.
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& grown =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(grown[0] == std::optional{5});
        CHECK(grown[1] == std::optional{7});
        CHECK(grown[2] == std::optional{9});

        std::vector<ChartNote> settled = notes;
        CHECK(sweepInertClaimedStops(settled, tempo_map).empty());

        // The control: without the finger the re-pick rides one span for its whole length, which
        // side ruling (ii) is what it exists for.
        const std::vector<ChartNote> without = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
        });
        const ChartShapes ridden = deriveFrom(without);
        REQUIRE(ridden.shapes.size() == 1);
        CHECK(ridden.shapes.front().sustain == Fraction{2});
    }
}

// TRAVEL SPLITS, AND A LANDED GRIP RE-OPENS (user ruling 2026-08-27, [D2], final form). A member's
// fret travel ends the span at the DEPARTURE — the last moment its channel states the posture's
// stop — and the grip its travels land in re-opens as a successor span whose members are the
// arrived rings. The travel between the two draws as the members' sliding tails and no span covers
// it, which is the published chord-slide picture: two fret stacks joined by parallel lines.
TEST_CASE("Chart shape derivation splits a span at a member's travel", "[core][chart]")
{
    SECTION("a chord slide states its departing grip, then the grip it lands in")
    {
        // Both members hold their stop through a restating keyframe at one beat, then travel and
        // come to rest at two beats. The departure is that restatement, so the box covers exactly
        // the beat the shape was held for.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{1}, 5}, {Fraction{2}, 7}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{1}, 7}, {Fraction{2}, 9}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{1});
        // The successor sits at the landing, which is no note slot at all — the one span in the
        // model that opens where nothing is struck and nothing is claimed.
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});

        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& departed =
            derived.postures[derived.shapes[0].posture].frets;
        CHECK(departed[0] == std::optional{5});
        CHECK(departed[1] == std::optional{7});
        // Bracket digits stating the LANDED grip, which is the whole reason the successor exists.
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{7});
        CHECK(landed[1] == std::optional{9});

        // The class, end to end: the departing shape was struck whole, so it is a box; the
        // successor strikes nothing at all, so its members arrive separately and it brackets.
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK(arpeggio[1]);
        // And it brackets through LAW III's own comparison rather than a claim: nothing is silently
        // held here, so the successor's arpeggio is trigger (a) at its purest.
        CHECK_FALSE(derived.shapes[1].silent_member);
        CHECK(derived.shapes[1].sounds_in_parts);
    }

    SECTION("a travel from the very first fret statement floors the span at the strike")
    {
        // No restating keyframe: the channel's first statement after the onset already names a
        // different stop, so the departure IS the onset and the box has no length to cover.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 9}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});
    }

    SECTION("edge (a): one member travelling re-opens the voicing it lands in")
    {
        // The ratified symmetry: a lone sliding finger under a held one still lands the hand in a
        // different chord, so the successor states it. What stayed put is INHERITED at its own
        // stop, exactly as a growth split inherits.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{7});
        CHECK(landed[1] == std::optional{7});
    }

    SECTION("edge (b): a travel straight into a restrike opens no successor")
    {
        // The chart's own encoding of "glides into that note": the arrival sits exactly one
        // minimum sustain distance before the landing's onset, and the ring ends at that onset.
        // The landed grip therefore has no moment of its own, and the strike's own full box states
        // the new chord.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{2}), {{Fraction{7, 4}, 10}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[1]);

        // The discrimination, one field apart: the same glide with a ring that BREATHES past its
        // landing does re-open, because there the grip is heard on its own.
        const std::vector<ChartNote> breathing = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{3}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{3}), {{Fraction{7, 4}, 10}}),
        });
        const ChartShapes landed = deriveFrom(breathing);
        REQUIRE(landed.shapes.size() == 2);
        CHECK(
            landed.shapes[1].position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{3, 4}});
        CHECK(landed.shapes[1].sustain == Fraction{5, 4});
    }

    SECTION("edge (c): staggered landings open no successor")
    {
        // The members come to rest a beat apart, so no single instant states a grip; the truth
        // stays in the sliding tails (watch item, docs/tracking/watch-items.md).
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{3}, 9}}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{});

        // The control, one offset apart: landing together is what re-opens.
        const std::vector<ChartNote> together = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 9}}),
        });
        CHECK(deriveFrom(together).shapes.size() == 2);
    }

    SECTION("edge (d): unequal travels landing together state whatever grip landed")
    {
        // A voice-leading slide: the two fingers move by different distances. Nothing in the rule
        // asks how far a finger went, only where it was last stated and where it comes to rest.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 8}}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{7});
        CHECK(landed[1] == std::optional{8});
    }

    SECTION("the successor runs by THE CONTINUITY LAW, ending at its first arrived gap")
    {
        // Its members are the arrived rings, so its extent is theirs: the shorter ring stops with
        // nothing sounding it, which is an authored detachment, and the survivor draws as an
        // ordinary remainder tail. The strum after that gap is its own statement and does not
        // stretch the bracket back over the silence.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{3}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{5}), {{Fraction{2}, 9}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{1})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 9, Fraction{1})),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(derived.shapes[2].position == GridPosition{.measure = 2, .beat = 1});
        CHECK(derived.shapes[2].sustain == Fraction{1});
    }

    SECTION("a glide with a held grip between its legs states each grip once")
    {
        // Three statements of the hand, and the model's own reading of the channel is what tells
        // them apart: equal frets are a HOLD, so the restated stop at two beats is a grip the
        // successor brackets, and the travel out of it splits that successor in turn.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{5}),
                {{Fraction{1}, 7}, {Fraction{2}, 7}, {Fraction{3}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{5}),
                {{Fraction{1}, 9}, {Fraction{2}, 9}, {Fraction{3}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(derived.shapes[2].position == GridPosition{.measure = 1, .beat = 4});
        CHECK(derived.shapes[2].sustain == Fraction{2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& middle =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(middle[0] == std::optional{7});
        CHECK(middle[1] == std::optional{9});
    }

    SECTION("a continuous multi-fret glide brackets only where it comes to rest")
    {
        // The same three fret statements with the middle one never restated: the channel leaves it
        // again, so it is a point on the path and not a grip. One travel, one landing.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{1}, 7}, {Fraction{2}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{1}, 9}, {Fraction{2}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{9});
        CHECK(landed[1] == std::optional{11});
    }
}

// Rule 12(a)'s fret, which is [D2]'s reading of the channel asked at a later slot: a carried ring
// joins the posture at the stop its own fret channel states THERE, so a finger that has slid since
// the strike is stated where it now is and one still sliding is stated nowhere.
TEST_CASE("A carried ring folds into a posture at the stop its channel states", "[core][chart]")
{
    SECTION("a travelled ring folds in at the fret it LANDED on")
    {
        // A chord slide landing on the beat and ringing long, and a chord on other strings a
        // measure-and-a-bit later. The carried strings are at 7 and 9 by then, and the frets they
        // were STRUCK at (5 and 7) are two spans behind the hand.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{8}), {{Fraction{1}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{8}), {{Fraction{1}, 9}}),
            noteAt(4, Fraction{}, 5, 3, Fraction{2}),
            noteAt(4, Fraction{}, 6, 3, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The departing stack, the grip its travels landed in, and the later chord that carries
        // that grip across its own onset.
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[2].position == GridPosition{.measure = 1, .beat = 4});
        REQUIRE(derived.shapes[2].posture < derived.postures.size());
        const std::vector<std::optional<int>>& carried =
            derived.postures[derived.shapes[2].posture].frets;
        REQUIRE(carried.size() >= 6);
        CHECK(carried[0] == std::optional{7});
        CHECK(carried[1] == std::optional{9});
        CHECK(carried[4] == std::optional{3});
        CHECK(carried[5] == std::optional{3});
        // The old answer, stated as its own assertion because it is the whole finding: the onset
        // frets are a grip the hand has left, and one chart cannot state two hand positions for
        // the same fingers at one instant.
        CHECK(carried[0] != std::optional{5});
        CHECK(carried[1] != std::optional{7});
    }

    SECTION("an untravelled ring folds in at its onset fret")
    {
        // The control, one keyframe apart: a channel that never leaves its stop still states that
        // stop, so the carry is unchanged.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{8}),
            noteAt(1, Fraction{}, 2, 7, Fraction{8}),
            noteAt(4, Fraction{}, 5, 3, Fraction{2}),
            noteAt(4, Fraction{}, 6, 3, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& carried =
            derived.postures[derived.shapes[1].posture].frets;
        REQUIRE(carried.size() >= 6);
        CHECK(carried[0] == std::optional{5});
        CHECK(carried[1] == std::optional{7});
    }

    SECTION("a ring caught MID-TRAVEL folds into no posture at all")
    {
        // ONE gliding ring — held at its stop through a restating keyframe one beat in, then
        // travelling to rest three beats later — crossed by the same chord at three instants. The
        // only thing that moves is how far along its channel the finger is: on the stop, between
        // stops, and on the grip it landed in.
        const auto crossed_at = [](const int measure, const int beat) {
            return streamOf({
                travellingAt(
                    noteAt(1, Fraction{}, 1, 5, Fraction{8}), {{Fraction{1}, 5}, {Fraction{4}, 7}}),
                inMeasure(measure, noteAt(beat, Fraction{}, 5, 3, Fraction{2})),
                inMeasure(measure, noteAt(beat, Fraction{}, 6, 3, Fraction{2})),
            });
        };
        // The posture of the one span each figure derives — the crossing chord's own.
        const auto carried_frets =
            [](const std::vector<ChartNote>& notes) -> std::vector<std::optional<int>> {
            const ChartShapes derived = deriveFrom(notes);
            REQUIRE(derived.shapes.size() == 1);
            REQUIRE(derived.shapes.front().posture < derived.postures.size());
            const std::vector<std::optional<int>>& frets =
                derived.postures[derived.shapes.front().posture].frets;
            REQUIRE(frets.size() >= 6);
            return frets;
        };

        // On the departure itself the channel still states the stop, so the carry is ordinary.
        const std::vector<std::optional<int>> at_departure = carried_frets(crossed_at(1, 2));
        CHECK(at_departure[0] == std::optional{5});
        // Between the departure and the landing the finger is on NO stop, so it is a member of
        // nothing: the posture states the struck strings and says nothing about this one.
        const std::vector<std::optional<int>> mid_travel = carried_frets(crossed_at(1, 4));
        CHECK_FALSE(mid_travel[0].has_value());
        CHECK(mid_travel[4] == std::optional{3});
        CHECK(mid_travel[5] == std::optional{3});
        // And from the landing on it states the grip it came to rest on.
        const std::vector<std::optional<int>> at_landing = carried_frets(crossed_at(2, 1));
        CHECK(at_landing[0] == std::optional{7});
    }
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

    SECTION("a stop the note's own PITCH speaks from is never inert")
    {
        // The lone tap above, tapping a HARMONIC over its held stop. The claim still reaches no
        // shape, and the field still cannot go: a harmonic speaks from the stopped length, so
        // clearing it would retune the note and leave its node level with the tapped point it
        // would then speak from — a record the rules refuse. The settle takes statements that
        // reach nothing, never the sound the charter wrote.
        ChartNote harmonic = tapHoldingAt(1, Fraction{}, 3, 17, Fraction{1}, 5);
        harmonic.harmonic_node = 17.0;
        std::vector<ChartNote> notes{harmonic};
        CHECK(sweepInertClaimedStops(notes, tempo_map).empty());
        REQUIRE(notes.size() == 1);
        CHECK(notes.front().held == std::optional{5});
    }

    SECTION("a held stop that states a shape is left alone")
    {
        // Two claims at one slot open a span, and the arrival at beat three PLAYS one of the stops
        // they state — which is the whole of what justifies a shape the hand alone stated. Without
        // it the span would dissolve and take both claims with it, which is the section above's
        // case rather than this one.
        std::vector<ChartNote> notes = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            tapHoldingAt(1, Fraction{}, 3, 12, Fraction{2}, 5),
            noteAt(3, Fraction{}, 1, 5, Fraction{1}),
        });
        CHECK(sweepInertClaimedStops(notes, tempo_map).empty());
        REQUIRE(notes.size() == 3);
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
