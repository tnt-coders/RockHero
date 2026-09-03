#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
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

// A pull-off onto a stated stop: the fretting hand releasing onto a fret a finger was already
// waiting on. That waiting finger is what the DERIVED held stop is read off — the connection the
// chart records IS the statement that the stop was down under the onset before it
// (\ref chartClaimedStops) — so a case wanting a derived claim writes the notation rather than the
// field.
[[nodiscard]] ChartNote pullOffAt(
    const int beat, const Fraction offset, const int string, const int fret, const Fraction ring)
{
    ChartNote note = noteAt(beat, offset, string, fret, ring);
    note.attack = NoteAttack::Legato;
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

// Where the note at one slot sits in a sorted stream, so a case names the note it means instead of
// an index the sort is free to move. Every per-note resolution vector is index-parallel to the
// stream, which is what makes one lookup enough for all of them.
[[nodiscard]] std::size_t indexAt(
    const std::vector<ChartNote>& stream, const int measure, const int beat, const int string)
{
    std::size_t at = 0;
    bool found = false;
    for (std::size_t index = 0; index < stream.size() && !found; ++index)
    {
        const ChartNote& note = stream[index];
        if (note.position.measure == measure && note.position.beat == beat && note.string == string)
        {
            at = index;
            found = true;
        }
    }
    REQUIRE(found);
    return at;
}

// One stream in the chart's own slot order. The cases below list their sounding notes and their
// held stops in whatever order reads best; this is what makes them a legal chart, so no case has
// to interleave two kinds of member by hand.
[[nodiscard]] std::vector<ChartNote> streamOf(std::vector<ChartNote> notes)
{
    std::ranges::sort(notes, chartNoteOrderLess);
    return notes;
}

// The derivation as every reader gets it, against a stated beat axis. Split from \ref deriveFrom
// so the one case whose question IS the meter can hand in its own map.
//
// The claims come from the one resolver every production caller uses (\ref chartClaimedStops), so
// a figure whose held stop is DERIVED from a pull-off derives here exactly as it does in the app.
[[nodiscard]] ChartShapes deriveWith(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    return deriveChartShapes(
        notes, chartClaimedStops(chartConnections(notes, tempo_map)), tempo_map);
}

// The derivation as every reader gets it: from the saved stream alone.
[[nodiscard]] ChartShapes deriveFrom(const std::vector<ChartNote>& notes)
{
    return deriveWith(notes, makeTempoMap());
}

// The CLASS both surfaces draw, asked end to end from one stream: the walk's own spans through the
// shared arrival rule, in span order. Derived rather than handed in, because a case that stated its
// own span and posture would be stating the very thing the class is a question about.
[[nodiscard]] std::vector<bool> arpeggiosFrom(const std::vector<ChartNote>& notes)
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartShapes derived = deriveWith(notes, tempo_map);
    const std::vector<ChartNote> presented =
        presentedChartNotes(chartConnections(notes, tempo_map), derived, tempo_map).notes;
    return chartShapeArrivals(presented, derived.shapes, tempo_map);
}

// THE INVARIANT ([D2] amended 2026-08-29), asked of a whole derivation: every span with a SOUNDING
// member is strictly positive. In a figure with no silently-held stops every span has one, so the
// observable form is "no span has zero length" — and it is asked through the emit path, over
// derived spans, rather than of a hand-built shape.
void everySpanIsPositive(const ChartShapes& derived)
{
    for (const ChartShape& shape : derived.shapes)
    {
        CHECK(shape.sustain.numerator > 0);
    }
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
// rings — to THE MUSICAL CLOSE, which where an event closes the span is that event's own onset.
// Rule 12a's margin is taken off this at the projection and is not in here (user ruling
// 2026-09-04).
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

    // One merged span from the first strum to the closing onset at 1:2+1/2 — the statement's own
    // reach lands exactly there, so both arms of the close agree. The drawn extent is a quarter
    // beat shorter and \ref makeChartViewState is where that happens.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes.front().sustain == Fraction{3, 2});
    CHECK(
        derived.shapes.front().closing_onset ==
        GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    CHECK(derived.shapes.front().posture == 0);
}

// The datum the display trim floors on. A box always reaches its final restrike even when the
// closing event crowds nearer than the margin, and since the trim moved to the projection (user
// ruling 2026-09-04) what the walk owes it is the last instant an EVENT stated the span. The close
// itself is unaffected by the crowding: it is the closing onset, wherever the last strum sits.
TEST_CASE(
    "Chart shape derivation publishes the last statement a closing trim floors on", "[core][chart]")
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

    // The close is the closing onset itself, a sixteenth past the restrike. The margin alone would
    // draw the span to 7/8 of a beat, before the beat-2 restrike it has to cover — which is exactly
    // what the published last statement stops (\ref ChartShape::stated_extent, pinned in
    // \ref makeChartViewState's own case).
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().sustain == Fraction{9, 8});
    CHECK(derived.shapes.front().stated_extent == Fraction{1});
}

// A span crowded closer than the margin closes where it closes: the musical close knows nothing
// about drawable room, so nothing here is at risk of collapsing. What used to be an exact-adjacency
// FALLBACK in the walk is now the projection's, asked of a trim that leaves nothing.
TEST_CASE("Chart shape derivation keeps a crowded span at positive length", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 3, Fraction{1, 8}),
        noteAt(1, Fraction{}, 2, 5, Fraction{1, 8}),
        noteAt(1, Fraction{1, 8}, 3, 7, Fraction{1, 8}),
    };
    const ChartShapes derived = deriveFrom(notes);

    // The closing onset lands exactly on the chord's own ring end: both arms of the close agree
    // there, and the crowding is the drawn extent's problem alone.
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
        // string 1 goes on ringing. Its survivor outlives the span and draws its own whole tail,
        // and the derivation's answer is the half beat the statement held for. The old maximum rule
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

    SECTION("a carried ring-through member the span DATES FROM bounds the extent")
    {
        // DELIBERATE FLIP (THE ACCUMULATION LAW, user ruling 2026-08-31). This section used to
        // assert that a carried ring never bounds a span. That rider still stands for the texture
        // it was written about — a ring crossing in from ground a preceding span already covered —
        // but here nothing precedes: the lone note at beat 1 is UNCOVERED, so the span DATES from
        // its onset (the dating rule) and it is a founding member, not texture crossing a
        // statement. A founding member bounds the span like any other, which is what makes the
        // posture truth criterion hold — the span may not go on claiming fret 7 after the finger
        // that held it stopped sounding.
        //
        // The chord rings on past that death, so its three members open a SEAMLESS
        // DEATH-SUCCESSOR and the figure is two statements tiling at 1.25 rather than one running
        // to 3. Three chord strings rather than two because a boundary successor is the opening
        // law asked at a boundary: its survivors must reach the accumulation minimum.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 7, Fraction{5, 4}),
            noteAt(2, Fraction{}, 1, 3, Fraction{2}),
            noteAt(2, Fraction{}, 2, 5, Fraction{2}),
            noteAt(2, Fraction{}, 4, 9, Fraction{2}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        // FOUNDING FOLLOWS COMPOSITION (user ruling 2026-08-31, review #10): the chord slot struck
        // three of the four stops this span holds, so it did not state the shape WHOLE — the
        // fourth arrived as a ring, which is members arriving staggered. The front says the same
        // thing from the other side: the span dates from the drone's own onset, a beat before the
        // strum.
        CHECK(derived.shapes[0].founding == SpanFounding::Accumulation);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{5, 4});
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].frets[2] == std::optional{7});
        // The successor: the three chord members, tiling onto the death with no gap and no mark.
        CHECK(derived.shapes[1].carry_opened);
        CHECK(derived.shapes[1].sustain == Fraction{7, 4});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[1].posture].frets[2] == std::nullopt);
    }

    SECTION("a carried ring crossing in over COVERED ground still bounds nothing")
    {
        // The rider's own population, and the control for the flip above: string 3's ring is
        // struck INSIDE a span of its own, so by the time it crosses the later chord its onset is
        // covered and it is texture rather than a founding member. It states its stop into the
        // posture and says nothing about how far the chord's statement runs.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 7, Fraction{9, 4}),
            noteAt(1, Fraction{}, 4, 9, Fraction{1, 8}),
            noteAt(3, Fraction{}, 1, 3, Fraction{2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        // The chord's own span: dated at its own slot, and running its struck members' two beats
        // even though the carried ring dies a quarter beat in.
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[1].posture].frets[2] == std::optional{7});
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
        // UPDATED for THE ACCUMULATION LAW (2026-08-31): the finding this section pins is
        // unchanged — the span still ends at EIGHT, where the fretting hand's own sound stopped,
        // and not at eleven where the tap run ends. What the law added is what happens AFTER that
        // end: three members ring on past it, so they open a seamless death-successor instead of
        // being left as bare tails. The discrimination is the first span's length, and it is
        // asserted below exactly as it was.
        // The corpus's own figure: a chord let ring while the tapping hand runs on one of its
        // strings. String 3's member sound ends at beat 8 and the run picks the string up from
        // there, tap ringing into tap. A chain written by those taps walked the span forward with
        // the run — to eleven, the last tap's ring — while the shape's own sound governs at eight.
        // The long members are what make the difference visible: with the tapped string's chain
        // the minimum either way, only its VALUE is in question. THREE of them, because the
        // successor they open is the opening law asked at the boundary and reads the accumulation
        // minimum.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{12}),
            noteAt(1, Fraction{}, 2, 7, Fraction{12}),
            noteAt(1, Fraction{}, 3, 9, Fraction{8}),
            noteAt(1, Fraction{}, 4, 11, Fraction{12}),
            inMeasure(3, tapAt(1, Fraction{}, 3, 14, Fraction{1})),
            inMeasure(3, tapAt(2, Fraction{}, 3, 14, Fraction{1})),
            inMeasure(3, tapAt(3, Fraction{}, 3, 14, Fraction{1})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes.front().sustain == Fraction{8});
        CHECK(derived.shapes[1].carry_opened);
        CHECK(derived.shapes[1].sustain == Fraction{4});
    }

    SECTION("the same run CARRYING the held stop states it, and still writes no length")
    {
        // The twin of the run above, one field different: every tap now states the stop the
        // fretting hand holds under it (string 3's fret 9). The claim NEITHER LENGTHENS NOR SPLITS
        // — it restates a stop the shape already makes, so no growth splits the span, and it says
        // where a finger is rather than how long anything sounds, so it writes no chain.
        //
        // What chains the statement across the run is the RING, exactly as in the plain twin above:
        // every sounding onset carries a member string's sound forward whichever hand made it, and
        // the held field is no part of that record. So the outcome is identical to the plain run —
        // the span ends at EIGHT where the fretting hand's own ring stopped, and the three long
        // members open the same seamless death-successor. That identity IS the discrimination: the
        // claim changed the posture's evidence and nothing about the extent.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{12}),
            noteAt(1, Fraction{}, 2, 7, Fraction{12}),
            noteAt(1, Fraction{}, 3, 9, Fraction{8}),
            noteAt(1, Fraction{}, 4, 11, Fraction{12}),
            inMeasure(3, tapHoldingAt(1, Fraction{}, 3, 14, Fraction{1}, 9)),
            inMeasure(3, tapHoldingAt(2, Fraction{}, 3, 14, Fraction{1}, 9)),
            inMeasure(3, tapHoldingAt(3, Fraction{}, 3, 14, Fraction{1}, 9)),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes.front().sustain == Fraction{8});
        CHECK(derived.shapes[1].carry_opened);
        CHECK(derived.shapes[1].sustain == Fraction{4});
    }

    SECTION("the same run PULLED OFF onto the held stop writes the length the taps could not")
    {
        // The third reading of the one figure, and the only one where the length moves. Each tap
        // is pulled off onto the stop the shape states on string 3, so the run holds nothing
        // authored and the taps' held stop is DERIVED as fret 9 — which the shape already states,
        // so the claims restate it and split nothing, exactly as the authored twin's claims do.
        // What moves the length is the other half of the notation: a pull-off is a FRETTING-HAND
        // onset sounding that stop, a member re-picking its own string, and a member strike writes
        // the chain a tap may not.
        //
        // So the statement rides the whole run to the chord's own end: ONE span, where the twin
        // above ends at eight and hands its survivors to a death-successor. The run reaches twelve
        // exactly, so nothing rings past the close and there is no successor to open.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{12}),
            noteAt(1, Fraction{}, 2, 7, Fraction{12}),
            noteAt(1, Fraction{}, 3, 9, Fraction{8}),
        };
        for (int beat = 1; beat <= 4; ++beat)
        {
            notes.push_back(inMeasure(3, tapAt(beat, Fraction{}, 3, 14, Fraction{1, 2})));
            notes.push_back(inMeasure(3, pullOffAt(beat, Fraction{1, 2}, 3, 9, Fraction{1, 2})));
        }
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{12});
        // The posture is the chord's, unmoved: the tapping hand's fret 14 states nothing about it,
        // and the pull-offs restate the stop it already holds.
        REQUIRE(derived.shapes.front().posture < derived.postures.size());
        const ChartPosture& posture = derived.postures[derived.shapes.front().posture];
        CHECK(posture.frets[0] == std::optional{5});
        CHECK(posture.frets[1] == std::optional{7});
        CHECK(posture.frets[2] == std::optional{9});
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

// A CHANGE IN ARTICULATION DOES NOT SPLIT THE SPAN (user ruling 2026-08-29, rule 11 amended in
// `docs/plans/in-progress/chart-ruleset.md`). The span is a fretting-hand statement, so the same
// frets played with a different technique restate it: one span, one posture. DELIBERATE FLIP —
// this case pinned the old rule ("splits a span on any articulation change") and now pins its
// reversal, which is the whole of what the amendment changed in this walk.
TEST_CASE("Chart shape derivation rides a span through an articulation change", "[core][chart]")
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

    // ONE span over both strums — the palm-muted restrike states the same strings at the same
    // stops, and its ring is adjacent to the one before it — closing at the lone note that ends it.
    // The posture table deduplicated by frets alone before the amendment and is untouched by it:
    // the technique never was part of a posture.
    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes.front().sustain == Fraction{3, 2});
    CHECK(derived.shapes.front().posture == 0);
    // Both strums sounded the shape WHOLE, so the class is untouched too (corollary 4): this is a
    // chord box that happens to change gesture, not an arpeggio.
    CHECK_FALSE(derived.shapes.front().sounds_in_parts);
}

// THE CHUG FIGURE END TO END, which is what the amendment exists for: a plain chord, dead chugs on
// the same grip, and the plain chord again are ONE hand fact and derive as ONE span. Census-scale
// — this is the population the corpus collapses by (docs/plans/in-progress/chart-ruleset.md, rule
// 11 amended 2026-08-29).
TEST_CASE("Chart shape derivation holds one span across a dead chug run", "[core][chart]")
{
    // Eight adjacent eighth-note strums of the same two-string grip: plain, six dead chugs, plain.
    std::vector<ChartNote> notes;
    for (int strum = 0; strum < 8; ++strum)
    {
        const Fraction offset{strum, 2};
        const bool chug = strum > 0 && strum < 7;
        ChartNote low = noteAt(1, offset, 1, 5, Fraction{1, 2});
        ChartNote high = noteAt(1, offset, 2, 7, Fraction{1, 2});
        low.dead = chug;
        high.dead = chug;
        low.palm_mute = chug;
        high.palm_mute = chug;
        notes.push_back(low);
        notes.push_back(high);
    }
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    // Strike into strike the whole way, through the last chug's own ring: beat 1 to beat 5.
    CHECK(derived.shapes.front().sustain == Fraction{4});
    CHECK_FALSE(derived.shapes.front().sounds_in_parts);
    CHECK_FALSE(derived.shapes.front().silent_member);
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

// What the lifecycle doc names under "Posture and shape derivation": every reader derives from the
// SETTLED stream, where the importer derived before `normalizeChart` ran. Under rule 11 amended
// (2026-08-29) the settle can no longer CHANGE this answer, and that is the point worth pinning —
// an articulation the rules refuse (a connection claim nothing justifies, here) never split a span
// the chart's own notes say is one, because no articulation does.
//
// DELIBERATE FLIP of the unsettled arm: it asserted two spans, which was the amendment's target.
TEST_CASE(
    "Chart shape derivation is unmoved by an articulation the settle rewrites", "[core][chart]")
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

    // Unsettled, the claim is an articulation like any other and rides: one span already.
    CHECK(deriveFrom(notes).shapes.size() == 1);

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
    // THE DATING RULE (user ruling 2026-08-31): the span dates from its earliest member onset not
    // covered by a preceding span, and the ringing string's own onset is that. Nothing precedes
    // it, so the FRONT is beat one and the strum arrives inside the statement rather than opening
    // it. This assertion used to read beat two, which was the slot the walk NOTICED the shape at.
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
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
//
// The two-member threshold is STATEMENT founding's, which is what a slot holding its whole shape
// at one instant is. Where the members arrive apart the accumulation minimum of three governs
// instead (signed 2026-09-04), which is why the claim-beside-rings section below is written at
// three: one law, two thresholds, and each section says which one it is asking about.
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

    SECTION("a claim beside two strings still RINGING opens a span; fewer members open nothing")
    {
        // THE ONE COUNT (user ruling 2026-08-31, review #2). The three kinds of member are counted
        // together, so a slot that STRIKES nothing still states a shape where a claim meets rings
        // crossing it — the case two counts in a disjunction could not see, since neither reached
        // the minimum on its own. The claim here rides a tap (one record stating two facts) and the
        // rings are two notes struck earlier, still sounding.
        //
        // The two rings are STAGGERED on purpose. Struck together they would state a shape by
        // themselves and the claim would GROW a standing span, which is a different law; arriving
        // apart they accumulate, and under the signed three-member minimum two of them open
        // nothing — so the slot the claim lands on is the first that states anything.
        const std::vector<ChartNote> stream = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3}),
            noteAt(1, Fraction{1, 2}, 3, 9, Fraction{5, 2}),
            tapHoldingAt(2, Fraction{}, 2, 14, Fraction{1}, 7),
        });
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        // ACCUMULATION by composition: the slot's own members are ONE, and the others arrived as
        // rings — so the front is the earliest of those onsets, a beat before the tap that
        // completed the shape.
        CHECK(derived.shapes.front().founding == SpanFounding::Accumulation);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{3});
        CHECK(derived.shapes.front().silent_member);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        CHECK(derived.postures.front().frets[2] == std::optional{9});
        CHECK(spanOfClaim(stream, derived, 2, 2) == std::optional<std::size_t>{0});

        // THE MINIMUM'S OWN DISCRIMINATION, one field apart: the second ring stops before the
        // claim's slot, so only two members meet there — and two accumulating members state
        // nothing (user signed the three-member minimum 2026-09-04).
        const ChartShapes two_members = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3}),
            noteAt(1, Fraction{1, 2}, 3, 9, Fraction{1, 2}),
            tapHoldingAt(2, Fraction{}, 2, 14, Fraction{1}, 7),
        }));
        CHECK(two_members.shapes.empty());

        // And the far end of the same count: both rings stopped leaves a lone claim, which states
        // nothing whatever the minimum is.
        const ChartShapes alone = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{1, 2}, 3, 9, Fraction{1, 2}),
            tapHoldingAt(2, Fraction{}, 2, 14, Fraction{1}, 7),
        }));
        CHECK(alone.shapes.empty());
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
        // Two shapes, one hold inside each. The surfaces place a hold's face wherever ITS span's
        // mark draws, so the mapping has to name the span rather than merely say that one exists.
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
        // is still waiting for its content. It is also a case where a hold's face is NOT at its own
        // slot: the bracket it prints under is this span's start, two beats earlier.
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
        // so the statement ended there and this re-pick JOINS nothing.
        //
        // UPDATED for THE ACCUMULATION LAW (2026-08-31): joining nothing is not the same as
        // stating nothing. The re-pick's own ring overlaps the strings still sounding, so members
        // hold a shape at that instant and the opening law opens one for them — dated at the
        // re-pick, because those onsets are covered by the span that just ended. The
        // discrimination this section exists for is the FIRST span's length, unchanged.
        //
        // A THIRD chord string rings through beside string 2, so the shape the re-pick joins
        // reaches the accumulation minimum; without it the re-pick's own ring and one survivor
        // are two members, which state nothing.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[0].sustain = Fraction{1, 2};
        notes.push_back(noteAt(1, Fraction{}, 4, 11, Fraction{2}));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes[1].founding == SpanFounding::Accumulation);
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

    SECTION("a re-pick with a different articulation rides the span")
    {
        // DELIBERATE FLIP (rule 11 amended 2026-08-29): this section asserted that a palm-muted
        // re-pick closed the span, because rule 11 split a chord on any articulation change and a
        // lone re-pick asked that same question of one string. The span is a fretting-hand
        // statement, so the palm of the OTHER hand cannot end it: the finger is still on fret 5.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].palm_mute = true;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{2});
    }

    SECTION("a re-pick at a DIFFERENT stop still closes the span")
    {
        // The control the flip above needs: position is what the comparison reads, so moving the
        // finger is still a contradiction the span cannot absorb — in EITHER founding (user ruling
        // 2026-08-31: absorption admits growth, never a stop the shape already states differently).
        // Without this the section above would pass on a rule that had stopped comparing anything.
        // What the moved finger then does is hold a shape with the strings still ringing beside
        // it, which the opening law brackets as an accumulation — a third chord string rings
        // through so that shape reaches the accumulation minimum.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].fret = 6;
        notes.push_back(noteAt(1, Fraction{}, 4, 11, Fraction{2}));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[1].founding == SpanFounding::Accumulation);
    }

    SECTION("a lone strike on a string the shape never held opens an accumulation")
    {
        // DELIBERATE FLIP (THE ACCUMULATION LAW, user ruling 2026-08-31). This section used to
        // read "closes the span", with the comment "this is the case only a hold marker can join,
        // which is why the ruling does not swallow it" — and the accumulation law is exactly the
        // pass that closed that gap. The strum's wholeness broke, so its STATEMENT-founded span
        // still splits; but the new stop and the chord's still-ringing members hold a shape
        // together, and two mutually overlapping rings at stated stops are what a span IS.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{2});
        notes[2].string = 3;
        notes[2].fret = 9;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        // The close is the re-pick's own onset: the two spans abut, and the margin between their
        // rails is taken at the projection.
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[1].founding == SpanFounding::Accumulation);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        // Every one of the chord's still-ringing strings is a member beside the new stop: the
        // maximal mutually-ringing set is what the law brackets, not the newest pair.
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& grown =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(grown[0] == std::optional{5});
        CHECK(grown[1] == std::optional{7});
        CHECK(grown[2] == std::optional{9});
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
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{1});
        // The stop the charter authored still prints inside the span it was authored in; what it
        // no longer does is swallow the note that contradicts it.
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].frets[2] == std::optional{5});
        // And the contradicting note holds a shape of its own with the chord still ringing under
        // it, which the accumulation law brackets (2026-08-31) — the same addition every other
        // section of this case gained.
        CHECK(derived.shapes[1].founding == SpanFounding::Accumulation);
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

    SECTION("a redundant hold over a ringing member is swept, and the ring keeps the span")
    {
        // DELIBERATE FLIP (THE ACCUMULATION LAW, user ruling 2026-08-31), and the finding is the
        // flip itself. This section used to pin the settle's SECOND round: string 2 rings through
        // the beat-2 onset, so the stop authored there restated the ring and stated nothing — and
        // it was ALSO what rule 10 counted as the shape's second member, so taking it away left
        // the slot a lone note, dissolved the span, and stranded the beat-3 hold in turn.
        //
        // The opening law took the second round's precondition away by making it impossible: the
        // very ring that makes the hold redundant is now a MEMBER, so the span the hold used to be
        // load-bearing for stands without it. The redundant hold is still swept in round one; the
        // beat-3 hold now joins a span that is really there, and states something.
        //
        // A cascade needs a claim that is inert AND the second member of a span, and the two
        // conditions no longer meet: a claim is inert only where it restates a stated stop, and
        // every way a stop gets stated (a strike at the slot, a ring crossing it, an earlier claim
        // of the same span) already carries its own member. That narrowing is reported as a
        // finding rather than hidden here; the sweep's own fixpoint loop is untouched.
        const auto [swept, removed] = sweep(streamOf({
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{2}),
            holdAt(2, Fraction{}, 2, 7),
            holdAt(3, Fraction{}, 3, 9),
        }));
        CHECK(removed == 1);
        REQUIRE(swept.size() == 3);
        // The one hold left is the beat-3 stop, which reaches a span and states a fret nothing
        // else states.
        CHECK(std::ranges::count_if(swept, [](const ChartNote& note) {
                  return silentHold(note.attack);
              }) == 1);
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
        // after the re-pick, and the span closes AT that chord — so the closing onset is what sits
        // exactly on the end, and the DRAWN extent lands back on the re-pick where its floor puts
        // it. A class re-derived from either window cannot tell the slot a statement RODE from the
        // slot that CLOSED it: 48 of the corpus's 305 lone-re-pick spans hold their re-pick only at
        // the drawn end, and 38 of those are the closing kind. The walk needs no such test, because
        // riding the slot is what it did.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{5, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{1, 4}, 1, 3, Fraction{1}),
            noteAt(2, Fraction{1, 4}, 2, 3, Fraction{1}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        // Five quarters of span from a start on beat one: the close lands on the crowding chord,
        // and the last statement a quarter beat behind it is where the drawn rails stop.
        CHECK(derived.shapes.front().sustain == Fraction{5, 4});
        CHECK(derived.shapes.front().stated_extent == Fraction{1});
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

// DERIVED HELD (user ruling 2026-08-31): a right-hand onset's held stop is DERIVED wherever a
// PULL-OFF states it, and the stored field is authoritative only where no such evidence exists.
// You cannot pull off onto a fret unless a finger was already waiting on it, so the connection the
// chart already records IS the statement that the fretting hand held that stop under the tap —
// writing it down beside the pull-off would be the same fact stated twice.
//
// The derivation reaches the span walk through the one resolver every reader uses
// (\ref chartClaimedStops), which is what these cases exercise: the tap below stores NO held at all
// and its claim still founds a shape.
TEST_CASE("A pull-off states the held stop under the onset it releases from", "[core][chart]")
{
    // A tap at the twelfth fret pulled off onto the fifth, over two strings still ringing. The
    // pull says a finger was on 5 while the tap sounded, so the tap CLAIMS 5 — and that claim
    // beside the two rings is a three-member shape the derivation opens.
    //
    // The two rings arrive APART, which is what keeps the claim the thing under test: struck
    // together they would state a shape by themselves and the claim would grow it, while
    // accumulating they stay two members and open nothing until a third joins them.
    const auto figure = [](const int landed) {
        ChartNote pull = noteAt(3, Fraction{}, 1, landed, Fraction{1});
        pull.attack = NoteAttack::Legato;
        return streamOf({
            noteAt(1, Fraction{}, 2, 7, Fraction{3}),
            noteAt(1, Fraction{1, 2}, 3, 9, Fraction{5, 2}),
            tapAt(2, Fraction{}, 1, 12, Fraction{1}),
            pull,
        });
    };

    SECTION("the derived stop joins the posture and publishes its face")
    {
        const std::vector<ChartNote> stream = figure(5);
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().sustain == Fraction{3});
        // The claim is what makes the tap's slot a shape at all, so the span carries a silently
        // stated member exactly as an authored `held` would have made it.
        CHECK(derived.shapes.front().silent_member);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{5});
        CHECK(derived.postures.front().frets[1] == std::optional{7});
        CHECK(derived.postures.front().frets[2] == std::optional{9});
        // The face: the derived claim reaches the span it stated into, so the surface that draws a
        // held digit has somewhere to put it — the same publication an authored claim gets.
        CHECK(derived.claim_shapes[indexAt(stream, 1, 2, 1)] == std::optional<std::size_t>{0});
    }

    SECTION("a HAMMER states nothing, because no finger has to be waiting for one")
    {
        // The discrimination, one fret apart: the connection now runs UP from the tap, which
        // states nothing about a stop under it. The tap claims nothing, its slot holds one lone
        // member, and the shape that does open is the one the hammered note itself sounds.
        const std::vector<ChartNote> stream = figure(15);
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK_FALSE(derived.shapes.front().silent_member);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{15});
        CHECK_FALSE(derived.claim_shapes[indexAt(stream, 1, 2, 1)].has_value());
    }

    SECTION("a pull onto an OPEN string states no finger at all")
    {
        // Fret zero asserts no stop, so there is nothing for the pull to have been waiting on: the
        // ring the open string already stores is the whole of what it says.
        const std::vector<ChartNote> stream = figure(0);
        const ChartShapes derived = deriveFrom(stream);
        CHECK_FALSE(derived.claim_shapes[indexAt(stream, 1, 2, 1)].has_value());
        REQUIRE(derived.shapes.size() == 1);
        CHECK_FALSE(derived.shapes.front().silent_member);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.postures.front().frets[0] == std::optional{0});
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

    SECTION("the ANSWERED claim reaches it, even from outside the extent the shape reaches")
    {
        // The case the answering half alone could not cover (user ruling 2026-08-31, blocker 3).
        // Two fingers come down at beat one with nothing sounding; a third joins the statement
        // still being ASSEMBLED at beat two; and a chord a quarter beat later PLAYS that third
        // stop and then states a shape of its own. The chord justifies the waiting statement — one
        // of its held frets played inside it — but the fretting hand PRESSES that fret, so the
        // chord claims nothing and has no face to publish; and a shape the hand alone stated
        // reaches only its own instant, so the close cannot see a finger that joined a beat after
        // it either. The record the justification is made of is the claim that was ANSWERED, and
        // it reaches the span it justified.
        std::vector<ChartNote> notes = streamOf({
            holdAt(1, Fraction{}, 1, 5),
            holdAt(1, Fraction{}, 2, 5),
            holdAt(2, Fraction{}, 3, 7),
            noteAt(2, Fraction{1, 4}, 3, 7, Fraction{1}),
            noteAt(2, Fraction{1, 4}, 4, 9, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);
        // The waiting statement at its own instant, and the chord's own shape after it.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes.front().silent_member);
        CHECK(
            derived.shapes.back().position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
        // The stream is sorted by (position, string), so the hold that JOINED at beat two is the
        // third record — named by its own fields rather than by its index alone.
        REQUIRE(notes.size() == 5);
        REQUIRE(silentHold(notes[2].attack));
        REQUIRE(notes[2].string == 3);
        CHECK(derived.claim_shapes[2] == std::optional<std::size_t>{0});

        // So one pass of the settle takes nothing, and a second derivation says exactly what the
        // first did: the fixpoint the one-pass sweep assumes holds by construction, rather than by
        // iterating until the figure stops dissolving itself.
        CHECK(sweepInertClaimedStops(notes, makeTempoMap()).empty());
        const ChartShapes again = deriveFrom(notes);
        REQUIRE(again.shapes.size() == derived.shapes.size());
        CHECK(again.shapes.front().position == derived.shapes.front().position);
        CHECK(again.shapes.front().sustain == derived.shapes.front().sustain);
        CHECK(again.claim_shapes[2] == std::optional<std::size_t>{0});
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
// The CLOSE is the same in both cases and always was the split's own instant: the two spans abut,
// and the replaced shape ends exactly where the successor starts. What differs from the silent-slot
// case is the DRAWN extent, because something sounds here — so the split's onset is published as a
// head to keep clear of (\ref ChartShape::closing_onset) and the projection takes rule 12a's margin
// off, where a slot of held fingers publishes none and the rails abut too.
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
        // The shape that was replaced ends AT the strum that replaced it, and publishes that strum
        // as the head its rails keep the margin from.
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[0].closing_onset == GridPosition{.measure = 1, .beat = 2});
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
        CHECK(derived.shapes[0].sustain == Fraction{1});
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
        CHECK(derived.shapes[0].sustain == Fraction{1});
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

// TRAVEL SPLITS AT THE LANDING, AND THE LANDED GRIP RE-OPENS THERE (user ruling 2026-08-27, [D2],
// AMENDED 2026-08-29). A chord slide keeps the fingers planted, so the rings run continuously and
// the span COVERS the transit, ending where the new grip is established — which is exactly where
// the successor opens, so the two TILE. The members' sliding tails draw across the transit, and
// the successor draws no opening mark of its own (amendment 2): the continued tails and the chord
// NAME changing at the landing are the whole statement.
TEST_CASE("Chart shape derivation splits a span at a member's travel", "[core][chart]")
{
    SECTION("a chord slide states its departing grip, then the grip it lands in")
    {
        // All three members hold their stop through a restating keyframe at one beat, then travel
        // and come to rest at two beats. [D2] AMENDED 2026-08-29: the box covers the hold AND the
        // transit, because the fingers stay planted through a chord slide and the rings run
        // continuously — the split falls at the LANDING, where the new grip is established, and
        // the two spans TILE with no gap between them.
        //
        // THREE strings, here and in every glide below: the successor a landing opens is the
        // opening law asked at a boundary, so its survivors must reach the signed accumulation
        // minimum before there is a landed grip to state.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{1}, 5}, {Fraction{2}, 7}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{1}, 7}, {Fraction{2}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{1}, 9}, {Fraction{2}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{2});
        // The successor sits at the landing, which is no note slot at all — the one span in the
        // model that opens where nothing is struck and nothing is claimed. It opens exactly where
        // its predecessor closes, which is what TILING means here.
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});

        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& departed =
            derived.postures[derived.shapes[0].posture].frets;
        CHECK(departed[0] == std::optional{5});
        CHECK(departed[1] == std::optional{7});
        CHECK(departed[2] == std::optional{9});
        // Bracket digits stating the LANDED grip, which is the whole reason the successor exists.
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{7});
        CHECK(landed[1] == std::optional{9});
        CHECK(landed[2] == std::optional{11});

        // The class, end to end: BOX at both ends (user ruling 2026-08-30, "that should not be an
        // arpeggio"). The departing shape was struck whole; nothing strikes the successor at all —
        // a LANDING IS NOT A SOUNDING, so no trigger fires there, and the published picture is two
        // boxes joined by the members' sliding tails.
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK_FALSE(arpeggio[1]);
        // Nothing is silently held here and nothing sounds inside the successor, so every trigger
        // is silent — the class is a box because no rule made it anything else.
        CHECK_FALSE(derived.shapes[1].silent_member);
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
    }

    SECTION("a travel from the very first fret statement still covers its own transit")
    {
        // No restating keyframe: the channel's first statement after the onset already names a
        // different stop, so the hand departs AT the strike. Under the departure split this span
        // had no length at all — the figure the amendment was ruled on ("having the initial span
        // 0 length almost feels more awkward than having it cover that transient state"). Under
        // the landing split the whole glide is the departing grip's span, and there is no
        // zero-length span left in the model to draw.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{2}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{2});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});
    }

    SECTION("edge (a): one member travelling re-opens the voicing it lands in")
    {
        // The ratified symmetry: a lone sliding finger under held ones still lands the hand in a
        // different chord, so the successor states it. What stayed put is INHERITED at its own
        // stop, exactly as a growth split inherits.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            noteAt(1, Fraction{}, 3, 9, Fraction{4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{2});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{7});
        CHECK(landed[1] == std::optional{7});
        CHECK(landed[2] == std::optional{9});
    }

    SECTION("edge (b): a travel landing into a FOREIGN chord opens no successor")
    {
        // The chart's own encoding of "glides into that note": the arrival sits exactly one
        // minimum sustain distance before the landing's onset, and the ring ends at that onset.
        // The landed grip therefore has no moment of its own, and the strike's own full box states
        // the new chord.
        //
        // Under tiling this is no test of its own ([D2] amended): the successor opens at the
        // landing and the next statement closes it one margin later, leaving it no room to be
        // DRAWN in — and a span no EVENT states, with no room of its own, states nothing and is not
        // emitted. This is the ONE derivation question that reads the display margin (user ruling
        // 2026-09-04, which moved every other use of it to the projection), and it does so on
        // purpose: whether the landed grip gets a moment of its own is whether a reader could see
        // one. The margin is taken at the CLOSING onset's measure, as every other reader takes it
        // and never at the arrival's (review F3); the signature-change section below is what pins
        // that.
        //
        // NARROWED 2026-08-29 (rule 11 amended, corollary 2): the closing statement has to be a
        // FOREIGN one. A restrike of the LANDED grip no longer closes the successor at all — it
        // restates it, so it rides inside it, which the section below pins.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{2}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{2}), {{Fraction{7, 4}, 11}}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        // The departing grip now covers its own glide, ending exactly at the landing it never
        // gets to state.
        CHECK(derived.shapes[0].sustain == Fraction{7, 4});
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
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{3}), {{Fraction{7, 4}, 11}}),
        });
        const ChartShapes landed = deriveFrom(breathing);
        REQUIRE(landed.shapes.size() == 2);
        CHECK(
            landed.shapes[1].position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{3, 4}});
        CHECK(landed.shapes[1].sustain == Fraction{5, 4});
    }

    SECTION("THE LANDING PIN: a landing met by a restrike emits exactly what it always did")
    {
        // The regression pin for the boundary rewrite (user ruling 2026-08-31, review #1): the
        // authority that judges a death changed — the continuity law alone now decides, where a
        // second reading of the same adjacency used to sit beside it — and a LANDING is the case
        // the two could have parted over, since a landing is a reach that is not a ring's end.
        //
        // They do not part, and the chart's own encoding is why: a fret-stating keyframe never
        // sits on a later onset of its own string, so a glide's arrival lands one margin BEFORE
        // the note it glides into and no slot ever sounds a member at its landing instant. The
        // whole emitted figure is therefore unchanged — the departing grip covers its transit and
        // ends at the landing, the successor is left no room and is never emitted, and the
        // restrike's own statement stands on its own.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{2}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{2}), {{Fraction{7, 4}, 11}}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // TWO spans and no third: the departing grip, and the chord that replaced it. The landed
        // grip's own successor is opened — three members survive the landing, so the opening law
        // admits it — and left no room, so it is never emitted, which is what the absence of any
        // carry-opened span below states.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{7, 4});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(std::ranges::none_of(derived.shapes, [](const ChartShape& shape) {
            return shape.carry_opened;
        }));
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& departing =
            derived.postures[derived.shapes[0].posture].frets;
        CHECK(departing[0] == std::optional{5});
        CHECK(departing[1] == std::optional{7});
        CHECK(departing[2] == std::optional{9});
        const std::vector<std::optional<int>>& replacing =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(replacing[0] == std::optional{3});
        CHECK(replacing[1] == std::optional{5});
        everySpanIsPositive(derived);
    }

    SECTION("a DROPPED successor is named by nothing in the claim ledger")
    {
        // The same figure with a silently-held finger under it. The hold's claim rides into the
        // successor the landing opens — the growth split's own carrying rule — and that successor
        // is then left no room and never emitted, so it is a span that exists for the walk and for
        // nobody else. The glide carries three strings so that successor is opened at all: a claim
        // has no ring, so it is the SURVIVORS that must reach the accumulation minimum.
        //
        // What a record REACHED is published when its span is PUSHED and at no other moment, so a
        // span that is never pushed is reached by nothing: the hold keeps the face of the statement
        // it was actually made in, and no entry in the ledger names an index the emitted spans do
        // not have.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{2}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 4, 11, Fraction{2}), {{Fraction{7, 4}, 13}}),
            holdAt(1, Fraction{}, 3, 9),
            noteAt(3, Fraction{}, 1, 3, Fraction{1}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(std::ranges::none_of(derived.shapes, [](const ChartShape& shape) {
            return shape.carry_opened;
        }));
        CHECK(spanOfClaim(notes, derived, 1, 3) == std::optional<std::size_t>{0});
        const auto names_an_emitted_span = [&derived](const std::optional<std::size_t>& reach) {
            return !reach.has_value() || *reach < derived.shapes.size();
        };
        CHECK(std::ranges::all_of(derived.claim_shapes, names_an_emitted_span));
    }

    SECTION("a full restrike of the landed grip rides INSIDE the successor")
    {
        // RULE 11 AMENDED 2026-08-29, corollary 2 (chart-ruleset.md): "a full restrike of a landed
        // grip stays INSIDE the successor — 'the bracket span never strums' dissolves; its full
        // box comes from the display law." The refusal was one identity comparison doing two
        // rules' work, and it died with the articulation it compared: the strike states the
        // successor's own stops, so it restates the shape like any other restrike.
        //
        // DELIBERATE FLIP: the same figure used to derive the departing span plus a fresh BOX span
        // at the restrike, with the successor suppressed for want of room (edge (b)). The
        // suppression still stands for a FOREIGN chord — the section above is that control.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{2}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{2}), {{Fraction{7, 4}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1}),
            noteAt(3, Fraction{}, 3, 11, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{7, 4});
        // ONE successor, opened at the landing and carried through the restrike's own ring.
        CHECK(
            derived.shapes[1].position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{3, 4}});
        CHECK(derived.shapes[1].sustain == Fraction{5, 4});
        CHECK(derived.shapes[1].carry_opened);
        // THE USER'S SIGHTING FIGURE, BOX CLASS END TO END (ruling 2026-08-30): a chord sliding
        // into chords "should not be an arpeggio". The landing fires no trigger — a landing is not
        // a sounding — and the restrike inside the successor is the shape WHOLE, so nothing makes
        // either span an arpeggio. What draws is two boxes joined by the members' sliding tails,
        // which is the published chord-slide picture.
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK_FALSE(arpeggio[1]);
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
    }

    SECTION("an interior lone re-pick makes the successor an arpeggio")
    {
        // The other side of the same law: a successor is classified by what sounds INSIDE it, so a
        // sounding that reaches only part of the landed grip is LAW III's class rule at its
        // ordinary width. One string of the landed grip is re-picked at its own landed stop, which
        // rides the successor (review F7) and states its members arriving separately. Every ring
        // runs past the re-pick, so the successor's statement is still in force there — a re-pick
        // AT the shortest member's end would close the span instead of riding it.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{7, 4}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].carry_opened);
        CHECK(derived.shapes[1].sounds_in_parts);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK(arpeggio[1]);
    }

    SECTION("edge (c): staggered landings open no successor")
    {
        // The members come to rest half a beat apart each, so no single instant states a grip; the
        // truth stays in the sliding tails (watch item, docs/tracking/watch-items.md). Three
        // members with three different landings, so the refusal is the STAGGERING and not the
        // member count — the control below lands the same three together and does re-open.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{5, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{3}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        // The EARLIEST landing ends the span ([D2] amended, edge (c)): coverage is the minimum of
        // the members' own, so the first finger to come to rest is where the shape stops being
        // the one that was travelling. The other fingers' remaining glide draws as their tails.
        CHECK(derived.shapes.front().sustain == Fraction{2});

        // The control, one offset apart on two of them: landing together is what re-opens.
        const std::vector<ChartNote> together = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{2}, 11}}),
        });
        CHECK(deriveFrom(together).shapes.size() == 2);
    }

    SECTION("edge (d): unequal travels landing together state whatever grip landed")
    {
        // A voice-leading slide: the fingers move by different distances. Nothing in the rule asks
        // how far a finger went, only where it was last stated and where it comes to rest.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{2}, 12}}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{7});
        CHECK(landed[1] == std::optional{8});
        CHECK(landed[2] == std::optional{12});
    }

    SECTION("the successor runs by THE CONTINUITY LAW, ending at its first arrived gap")
    {
        // Its members are the arrived rings, so its extent is theirs: the shorter ring stops with
        // nothing sounding it, which is an authored detachment, and the survivor draws its own
        // whole tail. The strum after that gap is its own statement and does not
        // stretch the bracket back over the silence.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{3}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{5}), {{Fraction{2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{5}), {{Fraction{2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{1})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 9, Fraction{1})),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].sustain == Fraction{2});
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
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{5}),
                {{Fraction{1}, 11}, {Fraction{2}, 11}, {Fraction{3}, 13}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        // Three statements, TILING end to end ([D2] amended): each covers its own hold and the
        // glide out of it, and the next opens exactly where the last closes.
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes[1].sustain == Fraction{2});
        CHECK(derived.shapes[2].position == GridPosition{.measure = 1, .beat = 4});
        CHECK(derived.shapes[2].sustain == Fraction{2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& middle =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(middle[0] == std::optional{7});
        CHECK(middle[1] == std::optional{9});
        CHECK(middle[2] == std::optional{11});
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
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{1}, 11}, {Fraction{2}, 13}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        // One travel, so one covered transit: the departing grip runs to the only landing there
        // is, and the transit fret at one beat gets no span of its own.
        CHECK(derived.shapes[0].sustain == Fraction{2});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& landed =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(landed[0] == std::optional{9});
        CHECK(landed[1] == std::optional{11});
        CHECK(landed[2] == std::optional{13});
    }
}

// [D2] AMENDED 2026-08-29 — the split moves to the LANDING. What the amendment BUYS, case by
// case: the transit rides the predecessor, so every slot inside a glide is inside a span; the two
// spans TILE; and no span with sound in it is ever emitted at zero length. Each section here is a
// hole the departure split left open, probed on a figure that derives the answer.
TEST_CASE("The landing split covers a travel and hands the grip over", "[core][chart]")
{
    // Three fingers hold a grip through a restating keyframe at one beat, glide, and come to rest
    // at two beats, ringing on to four. The departing span used to end at one beat; it now runs to
    // the landing at two, and the successor runs from there.
    //
    // THREE strings because the successor a landing opens is the opening law asked at a boundary:
    // its survivors must reach the signed accumulation minimum, so a two-string glide lands in no
    // stated grip at all.
    const auto chord_slide = [] {
        return std::vector<ChartNote>{
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{1}, 5}, {Fraction{2}, 7}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{1}, 7}, {Fraction{2}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{1}, 9}, {Fraction{2}, 11}}),
        };
    };

    SECTION("a picking-hand onset inside the travel flips the class, at the start and mid-glide")
    {
        // Trigger (d) asks whether a right-hand onset sounds INSIDE the span, and the departure
        // split answered it against an extent that stopped at the departure — so a tap at the very
        // slot the shape was struck at fell outside its own span (the span had no length to
        // contain it), and one taken mid-glide fell in the span-free transit. Both are inside the
        // covering span now.
        std::vector<ChartNote> plain = chord_slide();
        const std::vector<bool> untapped = arpeggiosFrom(streamOf(plain));
        REQUIRE(untapped.size() == 2);
        CHECK_FALSE(untapped[0]);

        std::vector<ChartNote> tapped_at_start = chord_slide();
        tapped_at_start.push_back(tapAt(1, Fraction{}, 5, 12, Fraction{1, 2}));
        const std::vector<bool> at_start = arpeggiosFrom(streamOf(tapped_at_start));
        REQUIRE(at_start.size() == 2);
        CHECK(at_start[0]);

        std::vector<ChartNote> tapped_mid_travel = chord_slide();
        tapped_mid_travel.push_back(tapAt(2, Fraction{1, 2}, 5, 12, Fraction{1, 2}));
        const std::vector<bool> mid_travel = arpeggiosFrom(streamOf(tapped_mid_travel));
        REQUIRE(mid_travel.size() == 2);
        CHECK(mid_travel[0]);
    }

    SECTION("a silent hold authored mid-travel states a shape and survives the settle")
    {
        // The span-free authoring dead zone, closed: a stop the charter states in the middle of a
        // glide falls inside a standing statement, so the ordinary growth law reaches it — the
        // finger comes down, the shape changes from that instant, and the split dates it where it
        // was written. It states something, so the inert sweep leaves it.
        //
        // The three spans TILE across the whole figure: the departing grip to the hold, the grown
        // shape through the rest of the glide, and the successor from the landing.
        std::vector<ChartNote> authored = chord_slide();
        authored.push_back(holdAt(2, Fraction{}, 4, 11));
        const std::vector<ChartNote> notes = streamOf(authored);
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(derived.shapes[2].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(spanOfHold(notes, derived, 2, 4) == std::optional<std::size_t>{1});
        CHECK(derived.shapes[1].silent_member);
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[1].posture].frets[3] == std::optional{11});

        const TempoMap tempo_map = makeTempoMap();
        std::vector<ChartNote> settled = notes;
        CHECK(sweepInertClaimedStops(settled, tempo_map).empty());
        CHECK(settled.size() == notes.size());
    }

    SECTION("open members restruck mid-slide ride the one span, per member and not per slot")
    {
        // The user's real-song figure (ruled 2026-08-29): a shape with fretted AND open members
        // slides, and the OPEN strings are restruck while it travels. An open channel never
        // departs, so those restrikes sound a stop the shape STILL STATES — an interior subset
        // sounding like any other. Judged per SLOT the travelling members' silence would truncate
        // the span at the first of them; judged per MEMBER, which is what the shape actually
        // states, the span rides through both restrikes and splits only at the landing.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 9}}),
            noteAt(1, Fraction{}, 3, 0, Fraction{4}),
            noteAt(1, Fraction{}, 4, 0, Fraction{4}),
            noteAt(2, Fraction{}, 3, 0, Fraction{3}),
            noteAt(2, Fraction{}, 4, 0, Fraction{3}),
            noteAt(2, Fraction{1, 2}, 3, 0, Fraction{5, 2}),
            noteAt(2, Fraction{1, 2}, 4, 0, Fraction{5, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{2});
        // The class the restrikes buy: each sounded two of the four strings the shape sounds, so
        // its members arrive separately — trigger (c), from inside the travel.
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(arpeggiosFrom(notes)[0]);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        everySpanIsPositive(derived);

        // The control, one stop apart: the same mid-slide sounding on a string the shape does not
        // state is a statement the span cannot absorb, so it truncates the travel there — and the
        // landing, now behind the close, opens nothing.
        const std::vector<ChartNote> foreign = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 9}}),
            noteAt(1, Fraction{}, 3, 0, Fraction{4}),
            noteAt(1, Fraction{}, 4, 0, Fraction{4}),
            noteAt(2, Fraction{}, 5, 3, Fraction{3}),
            noteAt(2, Fraction{}, 6, 3, Fraction{3}),
        });
        const ChartShapes truncated = deriveFrom(foreign);
        REQUIRE(truncated.shapes.size() == 2);
        CHECK(truncated.shapes[0].sustain == Fraction{1});
        CHECK(truncated.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        everySpanIsPositive(truncated);
    }

    SECTION("a lone re-pick of a landed member rides the successor")
    {
        // Review F7, ruled 2026-08-29. The successor's members are RINGS it never struck, so it
        // has no strike articulation for a re-pick to match — the same shape a silently-held
        // member has, and the same answer: the STOP is the whole test. Matching the carried record
        // whole was one comparison doing two rules' work, and it refused the lone re-pick that
        // rides every other span in the model.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{6}), {{Fraction{2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{6}), {{Fraction{2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{2})),
        });
        const ChartShapes derived = deriveFrom(notes);

        // Two spans, and the successor runs its full four beats: the re-pick RIDES it, so the
        // re-picked string's own new ring carries the statement on. The old answer closed the
        // successor at the re-pick instead — a bracket of seven quarter-beats, with the re-picked
        // note left inside no span at all.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{4});
        CHECK(derived.shapes[1].sustain != Fraction{7, 4});
        CHECK(derived.shapes[1].sounds_in_parts);

        // One string apart, a FULL restatement of the landed grip now RIDES the successor too
        // (rule 11 amended 2026-08-29, corollary 2). DELIBERATE FLIP: this control asserted a
        // third span here — "the bracket span never strums" — and that refusal dissolved with the
        // articulation identity it was made of. The re-pick and the full restrike are one law
        // again, which is what F7 said the separation was for.
        const std::vector<ChartNote> restruck = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{6}), {{Fraction{2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{6}), {{Fraction{2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{2})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 9, Fraction{2})),
            inMeasure(2, noteAt(1, Fraction{}, 3, 11, Fraction{2})),
        });
        const ChartShapes fresh = deriveFrom(restruck);
        REQUIRE(fresh.shapes.size() == 2);
        CHECK(fresh.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(fresh.shapes[1].carry_opened);
        everySpanIsPositive(fresh);
    }

    SECTION("only the landing successor is published as landing-opened")
    {
        // [D2] amendment 2's one derivational fact, and display keys the whole bracket deferral on
        // it. What makes it a field rather than a test a reader could run for itself is that every
        // available proxy drifts: a successor states nothing at its own start, but so does a span
        // the hand alone opened — and the walk's own record of "an event stated this" stops being
        // empty the moment an interior re-pick states the successor, which is exactly the case the
        // deferral has to survive.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{6}), {{Fraction{2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{6}), {{Fraction{2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{2})),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK_FALSE(derived.shapes[0].carry_opened);
        CHECK(derived.shapes[1].carry_opened);
        // And the re-pick that rides the successor does not clear it: the span is still the span
        // the landing opened, however many statements land inside it afterwards.
        CHECK(derived.shapes[1].sustain == Fraction{4});

        // The control the proxy would have failed: a GROWTH split states nothing by strike either,
        // but the claim that split it is an event at its own slot, so the bracket belongs there.
        std::vector<ChartNote> grown = chord_slide();
        grown.push_back(holdAt(2, Fraction{}, 4, 11));
        const ChartShapes with_growth = deriveFrom(streamOf(grown));
        REQUIRE(with_growth.shapes.size() == 3);
        CHECK_FALSE(with_growth.shapes[1].carry_opened);
        CHECK(with_growth.shapes[2].carry_opened);
    }

    SECTION("an authored hold at the departure splits the span the glide would have ridden")
    {
        // A chord slide keeps the fingers planted, so the continuity law covers the transit and the
        // span ends at the LANDING: two spans and no third. Nothing about the glide is published on
        // the span itself any more — `covers_travel` was the retired ink-ownership rule's carve-out
        // and went with the rule (the tail law never hides a ring that STATES something, so a
        // travelling member needs no span-level exemption) — and what the derivation still pins is
        // where the transit puts the boundaries.
        const ChartShapes sliding = deriveFrom(streamOf(chord_slide()));
        REQUIRE(sliding.shapes.size() == 2);

        // The extent decides it, not merely the channel. With a hold splitting the span AT the
        // departure, the first span ends exactly where the hand leaves and the grown shape beside
        // it is the one the glide runs under.
        std::vector<ChartNote> authored = chord_slide();
        authored.push_back(holdAt(2, Fraction{}, 4, 11));
        const ChartShapes split = deriveFrom(streamOf(authored));
        REQUIRE(split.shapes.size() == 3);

        // And a figure whose hand never moves is one span from end to end.
        const ChartShapes still = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
        }));
        REQUIRE(still.shapes.size() == 1);
    }

    SECTION("a slot inside the travel closes the span, and the landing it outran opens nothing")
    {
        // The whole of what a parked hand-off would have delivered, and why the walk needs none:
        // this lone onset ends the travelling statement before the hand arrives, so the successor
        // would open BEHIND the close — with no room, stating nothing either side does not. The
        // same law that suppresses edge (b) suppresses it, which is why nothing waits anywhere.
        //
        // UPDATED for THE ACCUMULATION LAW (2026-08-31): the finding is unchanged and asserted
        // below — the travelling statement still ends at the lone onset and its landing still opens
        // nothing.
        // What the lone onset now ALSO does is hold a shape with the two sliding rings, which the
        // opening law brackets; those members go on gliding under it, so the figure it leaves is
        // an accumulation whose own members then come to rest.
        std::vector<ChartNote> interrupted = chord_slide();
        interrupted.push_back(noteAt(2, Fraction{}, 5, 3, Fraction{1}));
        const ChartShapes derived = deriveFrom(streamOf(interrupted));

        // The EXACT count, before anything indexes: "the landing opens nothing" is a claim about
        // how many spans exist, so a bound that only guarded the indexing below would let another
        // span appear unnoticed (review #4, user ruling 2026-08-31). THREE is what the law derives
        // here — the travelling statement, the accumulation the lone onset holds with the sliding
        // rings, and the grip those rings finally come to rest in — and the landing the lone onset
        // outran contributes none of them.
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[1].founding == SpanFounding::Accumulation);
        everySpanIsPositive(derived);
    }

    SECTION("a landing on another chord's onset steals no box")
    {
        // The zero-length successor the review probed for: the glide lands exactly where a chord
        // on other strings is struck. A successor there would have no length at all, and drawing
        // it would put a bracket on the very instant the strike's own box states — so it is never
        // emitted, and the struck chord keeps its start and its whole extent.
        std::vector<ChartNote> landing_on_a_chord = chord_slide();
        landing_on_a_chord.push_back(noteAt(3, Fraction{}, 5, 3, Fraction{2}));
        landing_on_a_chord.push_back(noteAt(3, Fraction{}, 6, 3, Fraction{2}));
        const ChartShapes derived = deriveFrom(streamOf(landing_on_a_chord));

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{2});
        everySpanIsPositive(derived);

        // The discrimination, one beat apart: a landing that lands BEFORE the chord has room of
        // its own, and re-opens there.
        std::vector<ChartNote> landing_early = chord_slide();
        landing_early.push_back(inMeasure(2, noteAt(1, Fraction{}, 5, 3, Fraction{2})));
        landing_early.push_back(inMeasure(2, noteAt(1, Fraction{}, 6, 3, Fraction{2})));
        const ChartShapes breathing = deriveWith(streamOf(landing_early), makeTempoMap());
        REQUIRE(breathing.shapes.size() == 3);
        CHECK(breathing.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        everySpanIsPositive(breathing);
    }

    SECTION("the edge (b) suppression does not flip across a signature change")
    {
        // The first meter-change figure in this suite, and it is here because the OLD suppression
        // read rule 12a's margin at the ARRIVAL's measure while every other reader takes it at the
        // measure of the onset that closes the span (review F3) — a 6/8 glide landing in a 4/4 bar
        // is exactly where those two disagree. The suppression still reads a margin, since edge (b)
        // is a question about drawable room, and this is what pins WHICH margin: the closing
        // onset's, so a successor squeezed by a 4/4 head is judged against a 4/4 margin however
        // wide the bar it landed in.
        //
        // The arrival sits one 4/4 margin before the closing chord, which is how the chart states
        // "glides into that note"; measure 1 is 6/8, where that margin is twice as wide. The
        // closing chord is a FOREIGN grip since rule 11 was amended (2026-08-29, corollary 2): a
        // restrike of the LANDED grip restates the successor and rides inside it, so it would
        // close nothing and this figure would stop asking its question.
        const TempoMap meter_change{
            {TimeSignatureChange{.measure = 1, .numerator = 6, .denominator = 8},
             TimeSignatureChange{.measure = 2, .numerator = 4, .denominator = 4}},
            {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
             BeatAnchor{.measure = 5, .beat = 1, .seconds = 20.0}},
        };
        const std::vector<ChartNote> glide_into_chord = streamOf({
            travellingAt(noteAt(5, Fraction{}, 1, 5, Fraction{2}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(5, Fraction{}, 2, 7, Fraction{2}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(5, Fraction{}, 3, 9, Fraction{2}), {{Fraction{7, 4}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 3, Fraction{1})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 5, Fraction{1})),
        });
        const ChartShapes derived = deriveWith(glide_into_chord, meter_change);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 5});
        CHECK(derived.shapes[0].sustain == Fraction{7, 4});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 2, .beat = 1});
        everySpanIsPositive(derived);

        // The control, one ring apart: the same glide across the same signature change, with the
        // landing left to breathe. It re-opens, and the successor's own position is resolved on
        // the changed axis.
        const std::vector<ChartNote> breathing_glide = streamOf({
            travellingAt(noteAt(5, Fraction{}, 1, 5, Fraction{3}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(5, Fraction{}, 2, 7, Fraction{3}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(5, Fraction{}, 3, 9, Fraction{3}), {{Fraction{7, 4}, 11}}),
        });
        const ChartShapes landed = deriveWith(breathing_glide, meter_change);
        REQUIRE(landed.shapes.size() == 2);
        CHECK(
            landed.shapes[1].position ==
            GridPosition{.measure = 1, .beat = 6, .offset = Fraction{3, 4}});
        CHECK(landed.shapes[1].sustain == Fraction{5, 4});
        everySpanIsPositive(landed);
    }

    SECTION("no travel figure emits a span with sound in it and no length")
    {
        // THE INVARIANT at full strength, over every travel shape this suite has a figure for.
        // Under the departure split each of these opened with a zero-length span; none of them
        // can now, and the property is asked of the emit path rather than of a hand-built shape.
        everySpanIsPositive(deriveFrom(streamOf(chord_slide())));
        everySpanIsPositive(deriveFrom(streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{5, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{3}, 11}}),
        })));
        everySpanIsPositive(deriveFrom(streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{5}),
                {{Fraction{1}, 7}, {Fraction{2}, 7}, {Fraction{3}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{5}),
                {{Fraction{1}, 9}, {Fraction{2}, 9}, {Fraction{3}, 11}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{5}),
                {{Fraction{1}, 11}, {Fraction{2}, 11}, {Fraction{3}, 13}}),
        })));
        everySpanIsPositive(deriveFrom(streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{2}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{2}), {{Fraction{7, 4}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1}),
            noteAt(3, Fraction{}, 3, 11, Fraction{1}),
        })));
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
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{8}), {{Fraction{1}, 11}}),
            noteAt(4, Fraction{}, 5, 3, Fraction{2}),
            noteAt(4, Fraction{}, 6, 3, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The departing stack, the grip its travels landed in, the later chord that carries that
        // grip across its own onset — and, since THE ACCUMULATION LAW (2026-08-31), the span the
        // carried rings go on holding once that chord's own members fall silent, which is the
        // death-successor drawing no mark and naming what is still held.
        REQUIRE(derived.shapes.size() == 4);
        CHECK(derived.shapes[3].carry_opened);
        CHECK(derived.shapes[2].position == GridPosition{.measure = 1, .beat = 4});
        REQUIRE(derived.shapes[2].posture < derived.postures.size());
        const std::vector<std::optional<int>>& carried =
            derived.postures[derived.shapes[2].posture].frets;
        REQUIRE(carried.size() >= 6);
        CHECK(carried[0] == std::optional{7});
        CHECK(carried[1] == std::optional{9});
        CHECK(carried[2] == std::optional{11});
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
            noteAt(1, Fraction{}, 3, 9, Fraction{8}),
            noteAt(4, Fraction{}, 5, 3, Fraction{2}),
            noteAt(4, Fraction{}, 6, 3, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // Three now, not two: the carried rings outlive the later chord's own members, so they
        // open a death-successor when it ends (THE ACCUMULATION LAW, 2026-08-31). The control this
        // section exists for is the posture at index 1, unchanged.
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[2].carry_opened);
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

// THE ONE PER-STRING RECORD (N5 (a), user ruling 2026-08-30). The span used to hold two arrays over
// one fact — the STOPS each string states and the REACH each member's ring covers — and a string
// could be in one and not the other. These pin the seam's two halves now that it is gone: what an
// inert chain must NOT do (bound anything), and what it must go on doing (state the grip, and be
// seen when it moves).
TEST_CASE("Chart shape derivation holds one record per sounded string", "[core][chart]")
{
    SECTION("a fold-in whose ring ends mid-span bounds nothing")
    {
        // The carried string's ring stops a beat into the span. It is a MEMBER of the posture from
        // the start — its stop is in the grip — and it is extent-inert, so the span still runs to
        // its struck members' own ends. Give the fold-in a chain that bounds and the span would
        // truncate at the moment the let-ring texture went quiet, which is LAW III's whole point.
        //
        // THE CARRY IS MADE COVERED DELIBERATELY (THE ACCUMULATION LAW, 2026-08-31): inertness is
        // the dating rule's other half, so a ring the span would DATE FROM is a founding member
        // and bounds it. String 3 is therefore struck inside a span of its own, which is exactly
        // the texture-crossing-a-statement figure the [D3] rider was written about.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{9, 4}),
            noteAt(1, Fraction{}, 4, 11, Fraction{1, 8}),
            noteAt(3, Fraction{}, 1, 5, Fraction{3}),
            noteAt(3, Fraction{}, 2, 7, Fraction{3}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        // The carry states its stop in the posture...
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& frets =
            derived.postures[derived.shapes[1].posture].frets;
        CHECK(frets[2] == std::optional{9});
        // ...and bounds nothing: the span runs the struck members' three beats, not the one beat
        // the carry had left.
        CHECK(derived.shapes[1].sustain == Fraction{3});
    }

    SECTION("a growth split keeps a spent member's STOP while dropping its reach")
    {
        // A tap carries the two-hand run's SOUND past where the fretting hand's coverage stopped,
        // so a growth split can land in that window. The spent chain must go INERT, not away: its
        // reach is behind the split, and its stop is still one of the frets the hand holds.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3}),
            tapAt(2, Fraction{}, 1, 12, Fraction{2}),
            holdAt(3, Fraction{}, 3, 9),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<int>>& grown =
            derived.postures[derived.shapes[1].posture].frets;
        // String 1's own ring ended at beat 2, but the hand never left the stop: the grown grip
        // still states it, beside the string that carried on ringing and the finger just placed.
        CHECK(grown[0] == std::optional{5});
        CHECK(grown[1] == std::optional{7});
        CHECK(grown[2] == std::optional{9});
    }

    SECTION("a fold-in's own glide splits the span at the landing it makes")
    {
        // THE N5 FIGURE, and the defect the two records made unfixable: a lone ringing note folds
        // into a chord's posture and then GLIDES under it. Its travel lived in a record the span's
        // own reading could not see, so the span went on owning its members' tail ink across a
        // transit it did not know was happening — the ownership rule the tail law has since
        // replaced, which is why what this pins now is the derivation alone.
        // The carry HOLDS its stop through the fold-in slot (the restating keyframe at two beats)
        // and departs after it, so it is a member of the posture and then travels under the span
        // — which is the figure, rather than a string caught mid-glide that folds into nothing.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{2}, 9}, {Fraction{3}, 12}}),
            noteAt(2, Fraction{}, 1, 5, Fraction{3}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The span DATES from the ringing note (THE ACCUMULATION LAW, 2026-08-31), which is what
        // makes that ring a founding member and therefore what bounds the statement — so the
        // figure is the accumulation and the chord's own members hold the death-successor after
        // it.
        REQUIRE(derived.shapes.size() == 2);

        // The control, one keyframe apart: the same figure with the carry holding its stop states
        // no travel at all, so the statement runs end to end as one span.
        const std::vector<ChartNote> planted = streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{3}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3}),
        });
        const ChartShapes still = deriveFrom(planted);
        REQUIRE(still.shapes.size() == 1);
    }
}

// SPANS NEVER OVERLAP, which two independent readers both stand on (review N12). The presentation
// rules find a note's covering span with \ref SpanCover, which keeps the FURTHEST-REACHING span
// started at or before the onset; the highway's chord grouping keeps the LATEST-STARTING one. Those
// are the same span exactly while the ends are non-decreasing — that is, while no span outlives the
// start of the one after it — so the property is pinned here rather than assumed at two sites.
//
// The derivation makes it structural: a closing event ends a span at or before its own instant, a
// growth split divides one extent at the moment of the split, and a landing successor opens exactly
// where its predecessor closes. Every figure below is one of those shapes.
TEST_CASE("Chart shape derivation never overlaps two spans", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const auto tile = [&tempo_map](const std::vector<ChartNote>& notes) {
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() >= 2);
        for (std::size_t index = 1; index < derived.shapes.size(); ++index)
        {
            const ChartShape& earlier = derived.shapes[index - 1];
            const ChartShape& later = derived.shapes[index];
            const Fraction earlier_end =
                beatDistance(tempo_map, GridPosition{}, earlier.position) + earlier.sustain;
            const Fraction later_start = beatDistance(tempo_map, GridPosition{}, later.position);
            CHECK(!(later_start < earlier_end));
        }
    };

    SECTION("a chug run closed by a foreign chord")
    {
        tile(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            noteAt(3, Fraction{}, 1, 8, Fraction{2}),
            noteAt(3, Fraction{}, 2, 10, Fraction{2}),
        }));
    }

    SECTION("a growth split dividing one ring")
    {
        tile(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            holdAt(3, Fraction{}, 3, 9),
        }));
    }

    SECTION("a chord slide tiling into its landing successor")
    {
        tile(streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{7, 4}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1}),
        }));
    }
}

// THE OPENING MARK'S ANCHOR, published by the walk ([D2] amendment 2, refined by review F7). One
// field with one write rule: every span an EVENT states seeds it at its own start, and the first
// interior SOUNDING fills the slot a landing left empty.
TEST_CASE("Chart shape derivation publishes each span's opening mark", "[core][chart]")
{
    SECTION("an event-opened span carries its own start")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        // The strum states the shape, so the mark belongs at the strum — not at the restrike
        // inside, which states nothing the span did not already say.
        CHECK(
            derived.shapes.front().bracket_position ==
            std::optional{GridPosition{.measure = 1, .beat = 1}});
    }

    SECTION("a landing successor defers to its first interior sounding")
    {
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{7, 4}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{7, 4}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{7, 4}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].carry_opened);
        // The landing itself states nothing, so the mark waits for the re-pick.
        CHECK(
            derived.shapes[1].bracket_position ==
            std::optional{GridPosition{.measure = 1, .beat = 3}});
        CHECK(derived.shapes[1].position != *derived.shapes[1].bracket_position);
    }

    SECTION("a successor that never sounds interiorly carries no mark at all")
    {
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{4}), {{Fraction{2}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{4}), {{Fraction{2}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{4}), {{Fraction{2}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].carry_opened);
        CHECK_FALSE(derived.shapes[1].bracket_position.has_value());
    }
}

// THE ACCUMULATION LAW (user ruling 2026-08-31), probed figure by figure. Each section is one of
// the constraint figures the ruling was walked against, and what it pins is the DERIVATION's own
// answer for it — the front, the extent, the founding and the class — because those four are what
// every surface then reads.
TEST_CASE("Chart shape derivation opens a span where rings accumulate", "[core][chart]")
{
    // How many strings a span's posture holds, which is what the WITH_TOP styling convention keys
    // on downstream (highway_renderer.cpp: a 2-member arpeggio box draws no top border, 3+ draws
    // one, matching the chord boxes).
    const auto members = [](const ChartShapes& derived, const std::size_t shape) {
        REQUIRE(shape < derived.shapes.size());
        REQUIRE(derived.shapes[shape].posture < derived.postures.size());
        return static_cast<std::size_t>(std::ranges::count_if(
            derived.postures[derived.shapes[shape].posture].frets,
            [](const std::optional<int>& fret) { return fret.has_value(); }));
    };

    SECTION("THE BROKEN CHORD: one note at a time, bracketed from its FIRST note")
    {
        // The founding figure — the let-ring broken chord whose plucks pile up into a held shape,
        // and the one the whole ruling grew from. Under rule 10 alone this derived NOTHING: every
        // slot held one member, so no span ever opened and the figure read as four bare notes.
        // The opening law reads the rings instead, and the dating rule puts the bracket's front at
        // the FIRST pluck rather than at whichever arrival reached the threshold.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 3, Fraction{4}),
            noteAt(2, Fraction{}, 2, 5, Fraction{3}),
            noteAt(3, Fraction{}, 3, 5, Fraction{2}),
            noteAt(4, Fraction{}, 4, 4, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].founding == SpanFounding::Accumulation);
        // Every ring ends together at beat five, so the statement runs the whole figure and the
        // posture is the whole grip — the four stops the hand built up.
        CHECK(derived.shapes[0].sustain == Fraction{4});
        CHECK(members(derived, 0) == 4);
        // ARPEGGIO by construction, not by a rule of its own: the opening slot strikes one string
        // where the shape sounds four.
        CHECK(derived.shapes[0].sounds_in_parts);
        REQUIRE(arpeggiosFrom(notes).size() == 1);
        CHECK(arpeggiosFrom(notes).front());
        // The rails run from the front, so the mark does too, and no digit stack stands there for
        // members whose own heads are still to arrive.
        CHECK(derived.shapes[0].bracket_position == std::optional{derived.shapes[0].position});
        everySpanIsPositive(derived);
    }

    SECTION("THE FOUNDING MODE, both ways: a strum growth-splits where an accumulation absorbs")
    {
        // The discriminator, and the whole of what it decides. Both figures add a NEW stop to a
        // standing shape; they differ only in how that shape was founded.
        //
        // (a) STATEMENT: two strings struck together said "this grip, now", so a stop it does not
        //     state breaks the strum's wholeness and the span splits — the shipped law, unchanged.
        const std::vector<ChartNote> strum = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            noteAt(2, Fraction{}, 3, 9, Fraction{3}),
        });
        const ChartShapes stated = deriveFrom(strum);
        REQUIRE(stated.shapes.size() == 2);
        CHECK(stated.shapes[0].founding == SpanFounding::Statement);
        CHECK(stated.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(members(stated, 0) == 2);
        CHECK(stated.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(members(stated, 1) == 3);

        // (b) ACCUMULATION: the same three stops arriving one at a time. Arriving separately is
        //     what this statement IS, so the third stop is ABSORBED and the posture grows in
        //     place — one span, not two.
        const std::vector<ChartNote> built_up = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3}),
            noteAt(3, Fraction{}, 3, 9, Fraction{2}),
        });
        const ChartShapes absorbed = deriveFrom(built_up);
        REQUIRE(absorbed.shapes.size() == 1);
        CHECK(absorbed.shapes[0].founding == SpanFounding::Accumulation);
        CHECK(absorbed.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(members(absorbed, 0) == 3);
        CHECK(absorbed.shapes[0].sustain == Fraction{4});
    }

    SECTION("A CONTRADICTION splits either founding")
    {
        // The control the absorption needs: growth is admitted, a moved FINGER never is. The
        // last onset restates string 1 at another fret, which is a stop the shape already states
        // differently — and no founding absorbs that. Three strings accumulate first, because a
        // contradiction needs a STANDING span to split and two members state none.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3}),
            noteAt(2, Fraction{1, 2}, 3, 9, Fraction{5, 2}),
            noteAt(3, Fraction{}, 1, 8, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].founding == SpanFounding::Accumulation);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        // FOUNDING FOLLOWS COMPOSITION at a sound-driven split too (user ruling 2026-08-31, review
        // #10): the contradicting slot strikes ONE string and the shape it opens holds two, the
        // second folded in from the ring still crossing it. Every EVENT open re-derives the mode
        // from what the slot stated; only a growth split and a carry-opened successor inherit.
        CHECK(derived.shapes[1].founding == SpanFounding::Accumulation);
        everySpanIsPositive(derived);
    }

    SECTION("THE DEATH-SUCCESSOR CHAIN: the shape shrinks as its members fall silent")
    {
        // TERMINATION, and the posture truth criterion it enforces: no span claims a stop the
        // hand abandoned while it ran. Four members ring for three, four, five and six beats, so
        // the statement ends at the first death and the survivors go on holding a smaller shape —
        // seamlessly, with no opening mark, until fewer than three are left.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3}),
            noteAt(1, Fraction{}, 2, 7, Fraction{4}),
            noteAt(1, Fraction{}, 3, 9, Fraction{5}),
            noteAt(1, Fraction{}, 4, 11, Fraction{6}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{3});
        CHECK(members(derived, 0) == 4);
        CHECK_FALSE(derived.shapes[0].carry_opened);
        // The successor tiles onto the death exactly and holds what survived it.
        CHECK(derived.shapes[1].carry_opened);
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(members(derived, 1) == 3);
        // Nothing states a successor at its own start, so it draws no opening mark; with nothing
        // sounding inside it either, it draws none at all.
        CHECK_FALSE(derived.shapes[1].bracket_position.has_value());
        // And the chain ENDS where two members are left: fewer than the minimum hold no shape.
        everySpanIsPositive(derived);
    }

    SECTION("A REPLACEMENT is no death: a chug chain stays one statement")
    {
        // The discrimination the death law needs, and the reason the boundary test reads the
        // slot's own rings: these rings end exactly at their own restrikes, which is the
        // strike-into-strike shape a stored chug chain has. A boundary here would fracture every
        // repeated strum in the corpus.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].founding == SpanFounding::Statement);
        CHECK(derived.shapes[0].sustain == Fraction{3});
    }

    SECTION("A MIXED CHUG is a replacement too, where only ONE member's ring ends there")
    {
        // The discriminator (review #5, user ruling 2026-08-31): the section above cannot tell the
        // replacement reading from a boundary that simply never happens, because its rings all end
        // together and all are struck again. Here they do not — strings 2 and 3 ring straight
        // through beat 2, where string 1's ring ends and string 1 alone is restruck.
        //
        // The statement is in force at that instant (the one authority: every member is sounding,
        // string 1 by being replaced), so nothing dies and the whole grip runs on. Without the
        // replacement reading string 1 DIES there, the survivors open a death-successor, and the
        // posture after beat 2 loses fret 5 — which is what the posture assertion catches, since
        // both readings agree about where the figure ends.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3}),
            noteAt(1, Fraction{}, 3, 9, Fraction{3}),
            noteAt(2, Fraction{}, 1, 5, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{3});
        CHECK_FALSE(derived.shapes[0].carry_opened);
        CHECK(members(derived, 0) == 3);
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].frets[0] == std::optional{5});
    }

    SECTION("THE DATING RULE: a carry crossing covered ground never backdates")
    {
        // The 182-class figure, and the defect the rule was ruled to fix. String 3 is struck
        // inside a span of its own and rings on across the later chord; dating the chord's span
        // from that ring's onset would put it INSIDE a span already emitted, which is two
        // statements claiming one instant. The rule reads zero of them by construction, so what
        // this pins is the tiling: the second span starts at its own slot, at or after the first
        // one's end.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{4}),
            noteAt(1, Fraction{}, 4, 11, Fraction{1, 8}),
            noteAt(3, Fraction{}, 1, 3, Fraction{2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        // No overlap, which is the whole promise: the first span's end is at or before the
        // second's start.
        CHECK(derived.shapes[0].sustain <= Fraction{2});
        // And the carry that could not backdate is still a MEMBER: it states its stop into the
        // grip and says nothing about how far the chord's statement reaches.
        CHECK(members(derived, 1) == 3);
        CHECK(derived.shapes[1].sustain == Fraction{2});
    }

    SECTION("A DYAD accumulation states nothing: THREE members is the minimum")
    {
        // THE SIGNED MINIMUM (user sighted and signed 2026-09-04), and the one place it is pinned
        // head-on. A double-stop arpeggiated over its own ring is two members accumulating, which
        // was a real span while the minimum was the ruled threshold of two and reads as noise
        // beside the figures three members find. Sound may no longer found it; an AUTHORED
        // two-note span is the escape and is owed by docs/plans/todo/span-marker-redesign.md.
        //
        // Statement founding is untouched by this, which is why the arm is scoped to accumulation:
        // a struck dyad is still a chord box, pinned in the two-members-of-any-kind case above.
        const std::vector<ChartNote> dyad = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
        });
        CHECK(deriveFrom(dyad).shapes.empty());

        // The discrimination, one member apart: the same figure with a third stop accumulating
        // states a shape — and the count it lands on is what the with_top convention reads, since
        // a 2-member arpeggio box draws NO top border where 3+ draws one, through
        // ShapeViewState::strings (highway_renderer.cpp).
        const std::vector<ChartNote> triad = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3}),
            noteAt(2, Fraction{}, 2, 7, Fraction{2}),
            noteAt(3, Fraction{}, 3, 9, Fraction{1}),
        });
        const ChartShapes wider = deriveFrom(triad);
        REQUIRE(wider.shapes.size() == 1);
        CHECK(wider.shapes[0].founding == SpanFounding::Accumulation);
        CHECK(members(wider, 0) == 3);
        CHECK(arpeggiosFrom(triad).front());
    }

    SECTION("BROAD FOUNDING: an open string founds exactly as a fretted one does")
    {
        // The user's own re-derivation, which overruled the analysis's fretted-only boundary: the
        // bracket's claims are PER MEMBER, and an open member's 0 asserts no finger at all — only
        // the ring, which the chart already stores. So no member's claim can be false and the
        // open string founds.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 0, Fraction{3}),
            noteAt(2, Fraction{}, 2, 0, Fraction{2}),
            noteAt(3, Fraction{}, 3, 0, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].founding == SpanFounding::Accumulation);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        // All three members are open strings, so the minimum is reached by open members alone —
        // nothing fretted is doing the founding here.
        CHECK(members(derived, 0) == 3);
    }

    SECTION("THE DELIBERATE-EXTENSION DEFAULT: the rings' own evidence is ONE span")
    {
        // The user's figure, and the default the ruling chose for it: a player extending a held
        // shape one string at a time states nothing that would divide it, so the derivation reads
        // ONE statement — "the rings' own evidence" — rather than guessing at a phrase boundary.
        // A SPLIT-VERB OVERRIDE is recorded for later design; nothing here anticipates it, which
        // is what makes this the default rather than a decision.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{6}),
            noteAt(2, Fraction{}, 2, 5, Fraction{5}),
            noteAt(3, Fraction{}, 3, 6, Fraction{4}),
            noteAt(4, Fraction{}, 4, 7, Fraction{3}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{6});
        CHECK(members(derived, 0) == 4);
    }

    SECTION("DRONE UNDER MELODY: the drone rings on and the melody accumulates into it")
    {
        // One of the six constraint figures the tail-cap rule was verified against. Long open
        // drones under a single-string melodic line: every melody note overlaps them, so the
        // statement is the grip the hand actually holds, and it re-heads at each melody note's own
        // contradiction rather than fragmenting into nothing.
        //
        // TWO drones, entering apart: the pair alone is under the minimum and states nothing, so
        // the first melody note is still what founds the figure — which is the thing this section
        // is about.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 6, 0, Fraction{8}),
            noteAt(1, Fraction{1, 2}, 5, 0, Fraction{15, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(3, Fraction{}, 1, 7, Fraction{1}),
            noteAt(4, Fraction{}, 1, 8, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The drones found with the first melody note and the span dates from the earlier of their
        // onsets; each later melody note states a DIFFERENT stop on the same string, which is a
        // contradiction and re-heads. What never happens is a drone dropping out of the grip.
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].founding == SpanFounding::Accumulation);
        for (std::size_t shape = 0; shape < derived.shapes.size(); ++shape)
        {
            CAPTURE(shape);
            CHECK(members(derived, shape) == 3);
            REQUIRE(derived.shapes[shape].posture < derived.postures.size());
            CHECK(derived.postures[derived.shapes[shape].posture].frets[4] == std::optional{0});
            CHECK(derived.postures[derived.shapes[shape].posture].frets[5] == std::optional{0});
        }
        everySpanIsPositive(derived);
    }

    SECTION("DRONE UNDER STABS: the [D4] flip, as the accumulation law derives it")
    {
        // The other habitat the ruling named, and the one [D4]'s fold-in acceptance was argued
        // over: a ringing drone crossed by struck chords. The flip [D4] accepted is unchanged in
        // kind — the drone joins the posture and the span reads ARPEGGIO — and what the
        // accumulation law adds is the FRONT: the statement runs from the drone's own onset,
        // because that is where the figure began and nothing precedes it.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 6, 0, Fraction{6}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(2, Fraction{}, 2, 5, Fraction{1, 2}),
            noteAt(4, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(4, Fraction{}, 2, 5, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() >= 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        // FOUNDING FOLLOWS COMPOSITION (user ruling 2026-08-31, review #10): the stab struck two of
        // the three stops the span holds, so the slot did not state the shape WHOLE — the drone
        // arrived a beat earlier and the span dates from THERE. A simultaneous strike founds a
        // STATEMENT only where its own members are the whole shape, which is what makes the front
        // and the founding one story rather than two.
        CHECK(derived.shapes[0].founding == SpanFounding::Accumulation);
        CHECK(members(derived, 0) == 3);
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(arpeggiosFrom(notes).front());
        everySpanIsPositive(derived);
    }

    SECTION("THE MONSTER FIGURE IS BOUNDED: a long texture ends at its first member death")
    {
        // The 128-member relay the gate census condemned, in miniature. Under Rule A alone a
        // bracket could run for as long as ANY pair overlapped, and the census found conjunctions
        // spanning twenty measures. The death law is what makes that impossible: the statement
        // ends at the FIRST member to fall silent, so however long the texture runs, every fret
        // the bracket prints was held for every instant it covers.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{1, 2}, 2, 7, Fraction{8}),
            noteAt(2, Fraction{}, 3, 9, Fraction{8}),
            inMeasure(2, noteAt(1, Fraction{}, 4, 11, Fraction{8})),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The first member dies at beat three, so the span it founded stops there rather than
        // running to the far end of a texture it stopped being part of.
        REQUIRE(derived.shapes.size() >= 2);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{2});
        CHECK(members(derived, 0) == 3);
        // Nothing later claims the dead member's stop again.
        for (std::size_t shape = 1; shape < derived.shapes.size(); ++shape)
        {
            CAPTURE(shape);
            REQUIRE(derived.shapes[shape].posture < derived.postures.size());
            CHECK(derived.postures[derived.shapes[shape].posture].frets[0] == std::nullopt);
        }
        everySpanIsPositive(derived);
    }
}

// THE WALK-LEVEL SOUNDING TABLE (user ruling 2026-09-03, LAW A). A span records only its OWN
// members' rings, and a ring can outlive the span that covered it: a growth split supersedes the
// string whose stop the claim moved a finger off, and that string goes on sounding with no span
// recording it. To the grown span the string is silent ground, so a strike stating ANOTHER stop on
// it read as ordinary growth and an accumulation absorbed it — printing a posture over a finger
// that had demonstrably moved. The walk's own ring table is the witness, and it is END-INCLUSIVE
// because the same-string clamp puts the old ring's end exactly ON the contradicting strike: that
// junction instant is the only one at which the two coexist, so an exclusive read can never see it.
//
// Every assertion here pins POST-law behavior.
TEST_CASE("A foreign sounding ring contradicts a slot that restates its string", "[core][chart]")
{
    // The figure that makes a ring foreign, which is the only thing that can: three plucks
    // accumulate into a span, a silently-held stop on string one supersedes that string's stop and
    // splits the span, and string one's first ring goes on sounding under the grown span with no
    // chain recording it. It ends exactly at the beat that restates the string. Three plucks
    // rather than two because two accumulating members open no span for the hold to split.
    const auto figure = [](const int restated_fret) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{4}),
            noteAt(2, Fraction{1, 2}, 3, 9, Fraction{7, 2}),
            holdAt(3, Fraction{}, 1, 12),
            inMeasure(2, noteAt(1, Fraction{}, 1, restated_fret, Fraction{1})),
        });
    };

    SECTION("A CONTRADICTION over a foreign ring splits an accumulation")
    {
        // Fret 8 against a string still sounding fret 5: the finger moved, whatever the grown span
        // knows. Absorption is refused and the span closes at that beat.
        const ChartShapes derived = deriveFrom(figure(8));

        // POST-LAW: three spans — the accumulation, the piece the hold's supersession grew, and
        // the shape the contradiction founds. Absorbed, the middle piece would have run on to
        // string two's own ring end and there would be two.
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{2});
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        // POST-LAW: the grown piece ends AT the contradicting onset — the walk's own close, which
        // this law reuses and adds no path of its own to.
        CHECK(derived.shapes[1].sustain == Fraction{2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        // POST-LAW: and it prints the grip the hold stated, never the fret the contradiction takes.
        CHECK(derived.postures[derived.shapes[1].posture].frets[0] == std::optional{12});
        CHECK(derived.shapes[2].position == GridPosition{.measure = 2, .beat = 1});
        REQUIRE(derived.shapes[2].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[2].posture].frets[0] == std::optional{8});
        everySpanIsPositive(derived);
    }

    SECTION("A SAME-FRET restatement over a foreign ring never splits")
    {
        // The tie doctrine, and the control the arm above needs: the string is restated where it is
        // already sounding, so no finger moved and there is nothing to contradict. That the hold
        // dated a move to another fret is a claim, and claims are not what this arm reads.
        const ChartShapes derived = deriveFrom(figure(5));

        // POST-LAW: two spans, the restatement riding the grown one exactly as growth always did.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{3});
        everySpanIsPositive(derived);
    }

    SECTION("A span OPENING over a foreign ring is untouched")
    {
        // Nothing is contradicted until a span STATES the string, so the law is scoped to the join
        // and never to the open. String one's first ring ends exactly where the chord that follows
        // restates the string at another fret, and that chord opens its shape as it always has.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(3, Fraction{}, 1, 8, Fraction{2}),
            noteAt(3, Fraction{}, 2, 7, Fraction{2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // POST-LAW: one span, stated whole by the chord that opens it.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[0].founding == SpanFounding::Statement);
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].frets[0] == std::optional{8});
        CHECK(derived.postures[derived.shapes[0].posture].frets[1] == std::optional{7});
        everySpanIsPositive(derived);
    }

    SECTION("A SLIDE-OUT ring asserts no grip at its junction")
    {
        // The import twin's own exemption (LAW I), mirrored at read: by the ring's end the gliding
        // finger is off the board, so the fret the channel last stated is no standing grip and a
        // restrike at another fret contradicts nothing. Without the exemption this is byte-for-byte
        // the first section's figure and would split exactly as it does there.
        std::vector<ChartNote> notes = figure(8);
        notes[0].slide_out = -1;
        const ChartShapes derived = deriveFrom(notes);

        // POST-LAW: two spans — the restated slot absorbed as ordinary growth, the same picture
        // the same-fret section pins, reached by exemption rather than by agreement.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(derived.shapes[1].sustain == Fraction{3});
        everySpanIsPositive(derived);
    }
}

// The split re-judges through the walk's ONE gate and no other: the accumulation minimum decides
// which slots may OPEN a span, and a piece that never opened cannot emit. LAW A adds no second
// minimum check, so a figure that never musters the minimum states nothing at all.
//
// The figure is the case above thinned out, which is what makes it a control rather than a
// separate story: the same contradiction over the same foreign ring, with the accumulating
// partners gone or stopped short so no slot ever musters three members.
TEST_CASE("A contradiction split emits no piece the opening gate refused", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{4}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1}),
        holdAt(3, Fraction{}, 1, 12),
        inMeasure(2, noteAt(1, Fraction{}, 1, 8, Fraction{1})),
    });
    const ChartShapes derived = deriveFrom(notes);

    // POST-LAW: no slot in the figure musters three members, so nothing opens and nothing emits —
    // the contradiction has no span to split and manufactures none.
    CHECK(derived.shapes.empty());
}

// THE DATING CLAMP (LAW A's second half, user sighting 2026-09-03). The join refusal can only
// fire against a STANDING span, and the sighted reel figure musters its opening minimum only
// AFTER the junction — so the span used to open later and back-date its front across the instant
// the contradicted string audibly held another stop, and the fronted bracket claimed a grip that
// did not yet exist there. The junction record bounds the dating instead: a member behind the
// establishment of any stated stop rides extent-inert, and the span dates from the junction.
TEST_CASE("A span cannot date across the junction that established its grip", "[core][chart]")
{
    // The reel's own shape: string one sounds fret 7 up to the junction at beat 4, where the
    // restated fret replaces it; string two's long ring and the strike in measure 2 are what let
    // the accumulation muster three members — but only after the junction has passed with no span
    // standing to refuse anything.
    const auto figure = [](const int junction_fret) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 7, Fraction{3}),
            noteAt(2, Fraction{}, 2, 5, Fraction{4}),
            noteAt(4, Fraction{}, 1, junction_fret, Fraction{2}),
            inMeasure(2, noteAt(1, Fraction{}, 3, 5, Fraction{2})),
        });
    };

    SECTION("the junction bounds the front")
    {
        const ChartShapes derived = deriveFrom(figure(6));

        // POST-LAW: one span, dated at the junction where the 6's grip became possible — the
        // string-two member behind the bound is stated but extent-inert, exactly like a member
        // behind the coverage frontier. Pre-law the front back-dated to beat 2 and the bracket
        // claimed the 6 while string one still sounded 7.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 4});
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].frets[0] == std::optional{6});
        everySpanIsPositive(derived);
    }

    SECTION("a same-fret junction records nothing and the front dates freely")
    {
        // The control, differing ONLY by the restated fret: the tie doctrine establishes
        // nothing, so the ordinary dating stands and the span fronts at string two's onset. This
        // is also the discrimination proof — a dead clamp would date both sections alike.
        const ChartShapes derived = deriveFrom(figure(7));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 2});
        everySpanIsPositive(derived);
    }
}

// THE DEFAULT HELD FACT (user ruling 2026-09-02). A tap says nothing about the fretting hand, so
// asking what is under one always has an answer: the hand is holding whatever grip it is holding,
// and the tap's release lands on it. Inside a span that is the covering posture's fret on the tap's
// own string; span-less, or on a string the posture never names, it is 0 — the open string, nothing
// held. A FACT of the tap rather than presentation decoration, which is why it resolves here and
// every surface copies it.
TEST_CASE("A bare tap's held stop defaults to the grip the covering span holds", "[core][chart]")
{
    // One sounding note gives the span its extent, a silent hold states the grip on string 3, and
    // the taps sit inside it. The grip is SILENT deliberately: a tap is a real onset on its own
    // string, so a sounded grip's ring would be clamped at the tap and the span would end exactly
    // there — a boundary the case would then be about rather than the default.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{4}),
        holdAt(1, Fraction{}, 3, 7),
        tapAt(3, Fraction{}, 3, 12, Fraction{1}),
        tapAt(3, Fraction{}, 5, 12, Fraction{1}),
        inMeasure(3, tapAt(1, Fraction{}, 3, 12, Fraction{1})),
    });
    const ChartResolutions resolutions = chartResolutions(notes, makeTempoMap());

    // The span the defaults are read out of: one shape running the sounding member's whole ring,
    // stating fret 5 on string 1 and fret 7 on string 3 and nothing anywhere else.
    REQUIRE(resolutions.shapes.size() == 1);
    const ChartShape& span = resolutions.shapes.front();
    CHECK(span.position == GridPosition{.measure = 1, .beat = 1});
    CHECK(span.sustain == Fraction{4});
    REQUIRE(span.posture < resolutions.postures.size());
    const std::vector<std::optional<int>>& frets = resolutions.postures[span.posture].frets;
    REQUIRE(frets.size() >= 5);
    CHECK(frets[0] == std::optional{5});
    CHECK(frets[2] == std::optional{7});
    CHECK_FALSE(frets[4].has_value());

    SECTION("a tap under a posture that states its string releases onto that fret")
    {
        CHECK(resolutions.held_stops[indexAt(notes, 1, 3, 3)] == std::optional{7});
    }

    SECTION("a tap on a string the posture never names releases onto the open string")
    {
        // The same span, the same instant, one string over: the grip says nothing here, so nothing
        // is held here. Zero rather than absent, because the question still arose.
        CHECK(resolutions.held_stops[indexAt(notes, 1, 3, 5)] == std::optional{0});
    }

    SECTION("a span-less tap releases onto the open string")
    {
        // Past the span's whole reach, so no grip covers it at all.
        CHECK(resolutions.held_stops[indexAt(notes, 3, 1, 3)] == std::optional{0});
    }

    SECTION("only a right-hand onset takes one")
    {
        // The fretting hand's own onset IS the hand, and a silently-held stop is its own fret
        // (\ref claimedStop), so neither has a second stop underneath to answer for.
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 1, 1, 1)].has_value());
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 1, 1, 3)].has_value());
    }

    SECTION("THE CLAIM TIER IS UNTOUCHED: a default is not a statement the spans can read")
    {
        // The whole reason this is a table of its own. A default is read OUT of the postures, so
        // letting it into the claims would make every bare tap a member of the shape above it and
        // feed the derivation its own output. The tap claims nothing here and the posture above
        // has exactly two strings in it, which is that circularity not happening.
        CHECK_FALSE(resolutions.claimed_stops[indexAt(notes, 1, 3, 3)].has_value());
        CHECK_FALSE(resolutions.claimed_stops[indexAt(notes, 1, 3, 5)].has_value());
        CHECK_FALSE(resolutions.claim_shapes[indexAt(notes, 1, 3, 3)].has_value());
        const auto stated = static_cast<std::size_t>(std::ranges::count_if(
            frets, [](const std::optional<int>& fret) { return fret.has_value(); }));
        CHECK(stated == 2);
    }
}

// THE PRECEDENCE: an AUTHORED held stop and the one a PULL-OFF derives both beat the default, which
// only ever answers where the chart states nothing (user ruling 2026-09-02). The two upper tiers
// arrive already folded in their own ruled order (\ref chartClaimedStops), so what these cases pin
// is that the default is the LAST word and never a first one.
TEST_CASE("An authored or derived held stop beats the tap's default", "[core][chart]")
{
    // The same covering grip in every arm — fret 7 on string 3 — so any answer other than 7 is a
    // tier above the default having spoken.
    const auto figure = [](const std::optional<int> authored, const bool pulls_off) {
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            holdAt(1, Fraction{}, 3, 7),
            authored.has_value() ? tapHoldingAt(3, Fraction{}, 3, 12, Fraction{1}, *authored)
                                 : tapAt(3, Fraction{}, 3, 12, Fraction{1}),
        };
        if (pulls_off)
        {
            notes.push_back(pullOffAt(4, Fraction{}, 3, 5, Fraction{1}));
        }
        return streamOf(std::move(notes));
    };

    SECTION("the default is what the bare tap gets")
    {
        const std::vector<ChartNote> notes = figure(std::nullopt, /*pulls_off=*/false);
        const ChartResolutions resolutions = chartResolutions(notes, makeTempoMap());
        CHECK(resolutions.held_stops[indexAt(notes, 1, 3, 3)] == std::optional{7});
    }

    SECTION("a pull-off off the tap states the stop instead")
    {
        // One note apart from the arm above: a release onto fret 5 a beat later. A finger has to be
        // waiting on 5 to be pulled off onto it, so the notation states the stop and the covering
        // grip's 7 is not what was under this tap.
        const std::vector<ChartNote> notes = figure(std::nullopt, /*pulls_off=*/true);
        const ChartResolutions resolutions = chartResolutions(notes, makeTempoMap());
        const std::size_t tap = indexAt(notes, 1, 3, 3);
        REQUIRE(resolutions.derived_stops[tap] == std::optional{5});
        CHECK(resolutions.held_stops[tap] == std::optional{5});
    }

    SECTION("an authored stop states it instead")
    {
        // The charter typed 9 under this tap. Nothing derives here, so the field is what the chart
        // states and the covering grip's 7 is again not the answer.
        const std::vector<ChartNote> notes = figure(9, /*pulls_off=*/false);
        const ChartResolutions resolutions = chartResolutions(notes, makeTempoMap());
        const std::size_t tap = indexAt(notes, 1, 3, 3);
        CHECK_FALSE(resolutions.derived_stops[tap].has_value());
        CHECK(resolutions.held_stops[tap] == std::optional{9});
    }
}

// LIVE-DERIVED (user ruling 2026-09-02): the default is re-derived from whatever span covers the
// tap NOW, so an edit that reflows the spans around it moves the default with them. This falls out
// of per-revision recomputation — there is no stored value anywhere to go stale — and is pinned
// anyway, because the whole ruling rests on it.
TEST_CASE("The held default follows an edit that reflows the covering span", "[core][chart]")
{
    // One tap, never touched, under three different charts: the grip at 7, the same grip moved to
    // 9, and the grip gone. Only the NEIGHBOUR changes in each, which is what makes the tap's own
    // answer a derivation rather than a record.
    const auto figure = [](const std::optional<int> grip) {
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{4}),
            tapAt(3, Fraction{}, 3, 12, Fraction{1}),
        };
        if (grip.has_value())
        {
            notes.push_back(holdAt(1, Fraction{}, 3, *grip));
        }
        return streamOf(std::move(notes));
    };
    const auto defaultUnder = [&figure](const std::optional<int> grip) {
        const std::vector<ChartNote> notes = figure(grip);
        const ChartResolutions resolutions = chartResolutions(notes, makeTempoMap());
        return resolutions.held_stops[indexAt(notes, 1, 3, 3)];
    };

    // The grip moves and the tap's release moves with it.
    CHECK(defaultUnder(7) == std::optional{7});
    CHECK(defaultUnder(9) == std::optional{9});
    // And with the grip withdrawn there is no span left to cover the tap at all — one member states
    // no shape — so the release lands on the open string.
    CHECK(defaultUnder(std::nullopt) == std::optional{0});
}

} // namespace rock_hero::common::core
