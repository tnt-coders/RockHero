#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

using rock_hero::common::core::testing::anchor;
using rock_hero::common::core::testing::commonTime;
using rock_hero::common::core::testing::metersAt;

// Sixteen seconds of default 4/4, which is what every case here needs from the map: a stable
// grid and a minimum sustain distance of a fortieth of a whole note (50 ms at 120 BPM).
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
// where a span's extent lands and their answers are whole quarters apart.
[[nodiscard]] ChartNote inMeasure(const int measure, ChartNote note)
{
    note.position.measure = measure;
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

// A pull-off onto a stated stop: the fretting hand releasing onto a fret a finger was already
// waiting on. That waiting finger is what the DERIVED held stop is read off — the connection the
// chart records IS the statement that the stop was down under the onset before it
// (\ref chartPlantedStops).
[[nodiscard]] ChartNote pullOffAt(
    const int beat, const Fraction offset, const int string, const int fret, const Fraction ring)
{
    ChartNote note = noteAt(beat, offset, string, fret, ring);
    note.attack = NoteAttack::Legato;
    return note;
}

// A natural harmonic: fret 0 with a node, the fretting finger resting ON the wire.
[[nodiscard]] ChartNote harmonicAt(
    const int beat, const Fraction offset, const int string, const double node, const Fraction ring)
{
    ChartNote note = noteAt(beat, offset, string, 0, ring);
    note.harmonic_node = node;
    return note;
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

// The derivation as every reader gets it, against a stated tempo map. Split from \ref deriveFrom
// so the one case whose question IS the meter can hand in its own map.
//
// The claims are resolved inside the one derivation every production caller uses, so a figure
// whose held stop is DERIVED from a pull-off derives here exactly as it does in the app.
[[nodiscard]] ChartShapes deriveWith(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    const ChartConnections connections = chartConnections(notes, tempo_map);
    return deriveChartShapes(
        connections, HeadMargins{connections.places, tempo_map}, {}, tempo_map);
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
    return chartShapeArrivals(chartConnections(notes, tempo_map), derived.shapes, derived.postures);
}

// THE CHORD BOXES the 3D display derives for a stream, end to end: the walk's own spans through the
// shared projection into the one grouping both box treatments are decided in. Derived rather than
// handed in, because the question a box case asks — does the box key on the covering span's class?
// — is answered by the real path or not at all.
[[nodiscard]] HighwayChordGrouping boxesFrom(const std::vector<ChartNote>& notes)
{
    Chart chart;
    chart.notes = notes;
    Arrangement arrangement;
    arrangement.chart = std::move(chart);
    const ChartViewState view = makeChartViewState(arrangement, makeTempoMap());
    return makeHighwayChordGroups(view.notes, view.shapes);
}

// THE INVARIANT (shape rule 3), asked of a whole derivation: every span with a SOUNDING member is
// strictly positive. In a figure with no claimed stops every span has one, so the observable form
// is "no span has zero length" — and it is asked through the emit path, over derived spans, rather
// than of a hand-built shape.
void everySpanIsPositive(const ChartShapes& derived)
{
    for (const ChartShape& shape : derived.shapes)
    {
        CHECK(shape.sustain.numerator > 0);
    }
}

// A NATURAL HARMONIC STATES NO GRIP, asked of a whole derivation: no posture's grip holds a node
// touched with nothing pressed. A harmonic over a pressed stop states that stop, so a node never
// reaches a grip by any honest road.
void noPostureHoldsANaturalNode(const ChartShapes& derived)
{
    for (const ChartPosture& posture : derived.postures)
    {
        for (const std::optional<ChartStop>& stop : posture.stops)
        {
            const bool holds_node = stop.has_value() && stop->node.has_value();
            CHECK_FALSE(holds_node);
        }
    }
}

// The posture one derived span states, per string. Both indices are guarded here rather than at
// each of the sixty-odd call sites, so a slot naming a span or a posture that is not there fails as
// a Catch2 assertion instead of reading off the end.
[[nodiscard]] const std::vector<std::optional<ChartStop>>& derivedStops(
    const ChartShapes& derived, const std::size_t shape)
{
    REQUIRE(shape < derived.shapes.size());
    REQUIRE(derived.shapes[shape].posture < derived.postures.size());
    return derived.postures[derived.shapes[shape].posture].stops;
}

// Whether two streams derive THE SAME SHAPES: span for span, the front, the extent, whether the
// statement sounded in parts, the grip, and the class both surfaces draw. What a case asserting
// "this figure derives exactly as that one" needs — the law stated once, rather than one set of
// numbers written out twice and free to drift apart.
void derivesLike(const std::vector<ChartNote>& notes, const std::vector<ChartNote>& control)
{
    const ChartShapes derived = deriveFrom(notes);
    const ChartShapes expected = deriveFrom(control);
    REQUIRE(derived.shapes.size() == expected.shapes.size());
    for (std::size_t index = 0; index < derived.shapes.size(); ++index)
    {
        CHECK(derived.shapes[index].place == expected.shapes[index].place);
        CHECK(derived.shapes[index].sustain == expected.shapes[index].sustain);
        CHECK(derived.shapes[index].sounds_in_parts == expected.shapes[index].sounds_in_parts);
        CHECK(derivedStops(derived, index) == derivedStops(expected, index));
    }
    CHECK(arpeggiosFrom(notes) == arpeggiosFrom(control));
}

// How many strings one derived span's GRIP states — which is also the count the BRACKET prints,
// since the projection prints the grip alone (chart_projection.cpp): a tail sounding under the span
// prints no digit there. Bounds-guarded through \ref derivedStops, so a wrong slot is a Catch2
// failure rather than UB.
[[nodiscard]] std::size_t memberCount(const ChartShapes& derived, const std::size_t shape)
{
    return static_cast<std::size_t>(std::ranges::count_if(
        derivedStops(derived, shape),
        [](const std::optional<ChartStop>& stop) { return stop.has_value(); }));
}

// A note whose fret channel TRAVELS: the same slot note with fret statements added along its ring,
// listed as (offset, fret) pairs so a case reads as the path the hand takes. That path is the whole
// of what shape rule 4's two moments are read off, so nothing here is incidental either.
[[nodiscard]] ChartNote travellingAt(
    ChartNote note, const std::vector<std::pair<Fraction, int>>& path)
{
    for (const auto& [offset, fret] : path)
    {
        note.keyframes.push_back(Keyframe{.offset = offset, .fret = fret});
    }
    return note;
}

// THE DEFAULT HELD FACT. A tap says nothing about the fretting hand, so the hand is holding
// whatever grip it is holding, and the tap's release lands on it. Inside a span that is the
// covering posture's fret on the tap's own string; span-less, or on a string the posture never
// names, it is the open string, which no finger holds.
//
// THE CHUG FIGURE is how a legal chart reaches a covering span at all: a tap on a string the grip
// holds cuts that string's ring, and the span would end at the tap, but the grip RESTRUCK an
// eighth later renews every string and runs the span on over the tap. `grip` is the stop string 3
// holds; the tap on string 5 sits in the same span on a string it never names.
[[nodiscard]] std::vector<ChartNote> chugOverTap(const std::optional<int> grip)
{
    std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{3, 8}),
        noteAt(1, Fraction{}, 2, 7, Fraction{3, 8}),
        tapAt(2, Fraction{}, 3, 12, Fraction{1, 8}),
        tapAt(2, Fraction{}, 5, 12, Fraction{1, 8}),
        noteAt(2, Fraction{1, 2}, 1, 5, Fraction{5, 8}),
        noteAt(2, Fraction{1, 2}, 2, 7, Fraction{5, 8}),
        inMeasure(3, tapAt(1, Fraction{}, 3, 12, Fraction{1, 4})),
    };
    if (grip.has_value())
    {
        notes.push_back(noteAt(1, Fraction{}, 3, *grip, Fraction{1, 4}));
        notes.push_back(noteAt(2, Fraction{1, 2}, 3, *grip, Fraction{5, 8}));
    }
    return streamOf(std::move(notes));
}

// A note at any grid position, for the cases whose notes run past measure 1.
[[nodiscard]] ChartNote noteAt(
    const GridPosition position, const int string, const int fret, const Fraction ring)
{
    ChartNote note;
    note.position = position;
    note.string = string;
    note.fret = fret;
    note.sustain = ring;
    return note;
}

// A grid position, `offset` beats past the beat.
[[nodiscard]] GridPosition gridAt(
    const int measure, const int beat, const Fraction offset = Fraction{})
{
    return GridPosition{.measure = measure, .beat = beat, .offset = offset};
}

// An unnamed shape hold between two grid positions.
[[nodiscard]] ShapeHold holdOver(const GridPosition start, const GridPosition end)
{
    return ShapeHold{.start = start, .end = end, .grip = UnnamedGrip{}};
}

// The notes' resolutions under the holds, and whether each hold fits its notes: whether their
// normal form keeps it exactly as stated.
struct Held
{
    ChartResolutions resolutions;
    std::vector<bool> fits;
};

[[nodiscard]] Held deriveUnder(
    const std::vector<ChartNote>& notes, const std::vector<ShapeHold>& holds)
{
    const TempoMap tempo_map = makeTempoMap();
    const std::vector<ShapeHold> normal =
        normalizeShapeHolds(notes, holds, 0, tempo_map).resolved.shape_holds;
    Held held{.resolutions = chartResolutions(notes, holds, 0, tempo_map), .fits = {}};
    for (const ShapeHold& hold : holds)
    {
        held.fits.push_back(
            std::ranges::any_of(normal, [&hold](const ShapeHold& kept) { return kept == hold; }));
    }
    return held;
}

// The stop a resolved span's posture holds on a one-based string.
[[nodiscard]] std::optional<ChartStop> stopOf(
    const ChartResolutions& resolutions, const std::size_t span, const int string)
{
    return resolutions.postures[resolutions.shapes[span].posture]
        .stops[static_cast<std::size_t>(string - 1)];
}

} // namespace

// The core of shape rules 1 and 2: two or more fretting-hand strings struck together form a
// posture, consecutive strums of the same articulation merge into one span, and the span runs over
// the strums' own rings — to THE MUSICAL CLOSE, which where an event closes the span is that
// event's own onset. Shape rule 6's margin is taken off this at the projection and is not in here.
TEST_CASE("Chart shape derivation merges repeated strums of one posture", "[core][chart]")
{
    // A quarter-note power chord, the same chord again as an eighth an eighth later, then a
    // single note that closes the held posture.
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 8}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 8}),
        noteAt(2, Fraction{1, 2}, 3, 7, Fraction{1, 8}),
    };
    const ChartShapes derived = deriveFrom(notes);

    // One deduplicated posture: the two strums hold identical frets, so they share it.
    REQUIRE(derived.postures.size() == 1);
    // Indexed by string number, so the array is the model's string bound wide whatever this chart
    // plays — a slot no note fills stays empty rather than being trimmed away.
    REQUIRE(derived.postures.front().stops.size() == static_cast<std::size_t>(g_max_chart_strings));
    CHECK(derived.postures.front().stops[0] == std::optional{fretStop(5)});
    CHECK(derived.postures.front().stops[1] == std::optional{fretStop(7)});
    CHECK_FALSE(derived.postures.front().stops[2].has_value());

    // One merged span from the first strum to the closing onset at 1:2+1/2 — the statement's own
    // reach lands exactly there, so both arms of the close agree. The drawn extent is a
    // sixteenth shorter and \ref makeChartViewState is where that happens.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().place == Fraction{});
    CHECK(derived.shapes.front().sustain == Fraction{3, 8});
    CHECK(derived.shapes.front().closing_onset_place == Fraction{3, 8});
    CHECK(derived.shapes.front().posture == 0);
}

// The datum the display trim floors on. A box always reaches its final restrike even when the
// closing event crowds nearer than the margin, and since the trim lives at the projection what the
// walk owes it is the last instant an EVENT stated the span. The close itself is unaffected by the
// crowding: it is the closing onset, wherever the last strum sits.
TEST_CASE(
    "Chart shape derivation publishes the last statement a closing trim floors on", "[core][chart]")
{
    // The first strum rings exactly into the second (adjacency), which is what keeps the two in one
    // span at all under the continuity law — a stored gap there would end the statement and this
    // would be two boxes with nothing to floor.
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 32}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 32}),
        noteAt(2, Fraction{1, 8}, 3, 7, Fraction{1, 32}),
    };
    const ChartShapes derived = deriveFrom(notes);

    // The close is the closing onset itself, a sixteenth past the restrike. The margin alone would
    // draw the span to 7/8 of a quarter, before the beat-2 restrike it has to cover — which is
    // exactly what the published last statement stops (\ref ChartShape::stated_extent, pinned in
    // \ref makeChartViewState's own case).
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().sustain == Fraction{9, 32});
    CHECK(derived.shapes.front().stated_extent == Fraction{1, 4});
}

// A span crowded closer than the margin closes where it closes: the musical close knows nothing
// about drawable room, so nothing here is at risk of collapsing. The exact-adjacency FALLBACK is
// the projection's, asked of a trim that leaves nothing.
TEST_CASE("Chart shape derivation keeps a crowded span at positive length", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 3, Fraction{1, 32}),
        noteAt(1, Fraction{}, 2, 5, Fraction{1, 32}),
        noteAt(1, Fraction{1, 8}, 3, 7, Fraction{1, 32}),
    };
    const ChartShapes derived = deriveFrom(notes);

    // The closing onset lands exactly on the chord's own ring end: both arms of the close agree
    // there, and the crowding is the drawn extent's problem alone.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().sustain == Fraction{1, 32});
}

// THE CONTINUITY LAW (shape rule 3): a span's statement is in force while every SOUNDING member's
// STORED ring is continuous — ringing through, or ending exactly at its next same-string onset —
// and the FIRST genuine stored gap on any member ends the span at that ring's end. Min-extent is
// not a second rule beside it but this law's box case.
TEST_CASE("Chart shape derivation ends a span at the first stored gap", "[core][chart]")
{
    SECTION("a box with uneven rings ends at the first member to stop")
    {
        // The law's box case. String 2 stops an eighth in with nothing restriking it, which is
        // an authored statement of detachment: the shape stops being stated there, however long
        // string 1 goes on ringing. Its survivor outlives the span and draws its own whole tail,
        // and the derivation's answer is the eighth the statement held for.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 8}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().place == Fraction{});
        CHECK(derived.shapes.front().sustain == Fraction{1, 8});
    }

    SECTION("adjacency chains a whole run of strums into one span")
    {
        // The strike-into-strike shape a stored chug chain has: every ring ends exactly where the
        // next strum begins, so no member ever gaps and the run is one statement from the first
        // strum through the last strum's ring.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1, 8}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(derived.postures.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{5, 8});
    }

    SECTION("a stored gap between identical strums opens a second span")
    {
        // The discrimination for the run above, and the merge rule's own half of the law: two
        // strums of one articulation are one span only while the statement between them holds. A
        // gap is a boundary, not a pause — the span does not outlive its own sound waiting to be
        // rejoined by the next identical strum.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 8}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 8}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        // Still one posture: the frets never changed, only how long each statement about them
        // stayed in force.
        CHECK(derived.postures.size() == 1);
    }

    SECTION("a carried ring the span dates from bounds it, and its death opens nothing")
    {
        // The drone's onset is UNCOVERED, so the span DATES from it (the dating rule) and it is an
        // ordinary sounded member: every sounded member bounds, so the statement stops where the
        // drone does, and the span may not go on claiming fret 7 after the finger that held it
        // stopped sounding.
        //
        // Shape rule 1: the chord's three strings ring on past that death and open NOTHING.
        // Ring-out opens nothing, and what those survivors draw is plain tails.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 7, Fraction{5, 16}),
            noteAt(2, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(2, Fraction{}, 2, 5, Fraction{1, 2}),
            noteAt(2, Fraction{}, 4, 9, Fraction{1, 2}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{5, 16});
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].stops[2] == std::optional{fretStop(7)});
        // The survivors state nothing of their own: no second span anywhere in the figure.
        CHECK(std::ranges::none_of(derived.shapes, [](const ChartShape& shape) {
            return shape.landing_opened;
        }));
    }

    SECTION("a fretted ring crossing in over COVERED ground is carried, and its death splits")
    {
        // The carry was struck inside a span already emitted, which closed a thirty-second in. It
        // founds nothing and dates nothing (shape rule 1): the chord founds on its own two strokes
        // and dates from them, at the half. A fretted ring still sounding there is a finger down,
        // so it joins the chord's grip as a carried member and bounds it like every member, making
        // the figure sound in parts. Its death a sixteenth on is a SPLIT, not a close: the chord
        // runs on alone from there to its own ring's end, a box, since its own stroke was whole.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 7, Fraction{9, 16}),
            noteAt(1, Fraction{}, 4, 9, Fraction{1, 32}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].sustain == Fraction{1, 32});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 16});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(memberCount(derived, 1) == 3);
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(7)});
        CHECK(derived.shapes[2].place == Fraction{9, 16});
        CHECK(derived.shapes[2].sustain == Fraction{7, 16});
        CHECK_FALSE(derived.shapes[2].sounds_in_parts);
        CHECK(memberCount(derived, 2) == 2);

        // The control one ring apart: the carry outliving the chord's slot by a whole quarter.
        // It still joins and bounds, so the split moves with its death, a quarter in.
        const ChartShapes discriminating = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{3, 4}),
            noteAt(1, Fraction{}, 4, 11, Fraction{1, 32}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 2}),
        }));
        REQUIRE(discriminating.shapes.size() == 3);
        CHECK(discriminating.shapes[1].place == Fraction{1, 2});
        CHECK(discriminating.shapes[1].sustain == Fraction{1, 4});
        CHECK(discriminating.shapes[2].place == Fraction{3, 4});
        CHECK(discriminating.shapes[2].sustain == Fraction{1, 4});
    }

    SECTION("an open ring crossing in over COVERED ground is carried as a fretted one is")
    {
        // The same figure with the carry an open string: still sounding where the chord founds,
        // it is part of the sounding shape (shape rule 1), so it joins the grip as a carried
        // member, and its death splits the chord's span exactly as the fretted carry's does.
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 3, 0, Fraction{9, 16}),
            noteAt(1, Fraction{}, 4, 9, Fraction{1, 32}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 2}),
        }));
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 16});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(0)});
        CHECK(derived.shapes[2].place == Fraction{9, 16});
        CHECK(derived.shapes[2].sustain == Fraction{7, 16});
        CHECK_FALSE(derived.shapes[2].sounds_in_parts);
        CHECK_FALSE(derivedStops(derived, 2)[2].has_value());
    }
}

// THE CONTINUITY LAW's other half, and the one a right-hand onset must never cross: a sounding
// onset of either hand CONTINUES a chain's statement, but only a MEMBER sounding WRITES its
// length. A tap says nothing about the fretting hand — it joins no posture for exactly that
// reason — so a tap's own ring can never be how far the SHAPE reaches.
//
// Every case here is a tap landing exactly where a member's ring ends, which is the only shape the
// stream can legally take: a tap is a real onset on its own string, so that string's stored ring
// was already clamped to it. Continuity therefore stands at each of these taps, and the chain does
// not take the tap's ring with it, so the member's own ring governs. Each section names the answer
// a tap-written chain would give instead.
TEST_CASE("Chart shape derivation never lets a tap write a span's extent", "[core][chart]")
{
    SECTION("a tapped sixteenth does not decide a chord's extent")
    {
        // The chord rings four quarters and the tapping hand articulates it at the far end. A chain
        // written by those taps would end the span a sixteenth PAST the chord's own sound, which is
        // a statement about the shape read off the wrong hand entirely.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            inMeasure(2, tapAt(1, Fraction{}, 1, 12, Fraction{1, 64})),
            inMeasure(2, tapAt(1, Fraction{}, 2, 12, Fraction{1, 64})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().place == Fraction{});
        CHECK(derived.shapes.front().sustain == Fraction{1});
    }

    SECTION("a long tap does not carry a span past the fretting hand's last sound")
    {
        // The same figure with the taps ringing on: two quarters of tapped sound after the chord's
        // six. A chain written by the taps would run the span to eight — the hand holds for six
        // quarters and the record would claim eight, which is the lengthening the split forbids.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3, 2}),
            inMeasure(2, tapAt(3, Fraction{}, 1, 12, Fraction{1, 2})),
            inMeasure(2, tapAt(3, Fraction{}, 2, 12, Fraction{1, 2})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{3, 2});
    }

    SECTION("a tap run does not hold a span past the member's own ring")
    {
        // The span ends at EIGHT, where the fretting hand's own sound stopped, and not at eleven
        // where the tap run ends. The corpus's own figure: a chord let ring while the tapping hand
        // runs on one of its strings. String 3's member sound ends at beat 8 and the run picks the
        // string up from there, tap ringing into tap. A chain written by those taps would walk the
        // span forward with the run — to eleven, the last tap's ring — while the shape's own sound
        // governs at eight. The members are what make the difference visible: with the tapped
        // string's chain the minimum either way, only its VALUE is in question.
        //
        // Shape rule 1, and it is the two reach columns doing their two jobs: the tap RENEWS string
        // 3's sound at beat 8, so nothing quits and the grip never breaks — while `covers`, the
        // fretting hand's own reach, is what the close reads, so the statement runs out at eight.
        // ONE span: ring-out opens nothing, and the three long members ring on as plain tails.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3}),
            noteAt(1, Fraction{}, 3, 9, Fraction{2}),
            noteAt(1, Fraction{}, 4, 11, Fraction{3}),
            inMeasure(3, tapAt(1, Fraction{}, 3, 14, Fraction{1, 4})),
            inMeasure(3, tapAt(2, Fraction{}, 3, 14, Fraction{1, 4})),
            inMeasure(3, tapAt(3, Fraction{}, 3, 14, Fraction{1, 4})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{2});
    }

    SECTION("the same run PULLED OFF onto the held stop writes the length the taps could not")
    {
        // The third reading of the one figure, and the only one where the length moves. Each tap
        // is pulled off onto the stop the shape states on string 3, so the taps' held stop is
        // DERIVED as fret 9 — which the shape already states, so the claims restate it and split
        // nothing, exactly as the pressed twin's claims do.
        // What moves the length is the other half of the notation: a pull-off is a FRETTING-HAND
        // onset sounding that stop, a member re-picking its own string, and a member strike writes
        // the chain a tap may not.
        //
        // So the statement's coverage runs the whole figure to the chord's own end — reaching
        // TWELVE where the twin above reaches eight — because the run's own pull-offs carry the
        // fretting hand that far, which the taps alone never could. And it is ONE span: the first
        // tap sounds the chord's own string-3 stop under its still-ringing neighbours, which is
        // same-hold material sounding in parts inside the stroke's own rings, so THE ABSORPTION
        // RULE takes the chord in as the figure's opening stroke rather than letting it stand
        // alone as a box for the pull-off to break — the tap's claim reads exactly as it reads at
        // the slot open (the corpus figure is Periphery, "It's Only Smiles" measure 82; read the
        // other way, the bracket would start at the first pull-off instead of at the chord).
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{3}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3}),
            noteAt(1, Fraction{}, 3, 9, Fraction{2}),
        };
        for (int beat = 1; beat <= 4; ++beat)
        {
            notes.push_back(inMeasure(3, tapAt(beat, Fraction{}, 3, 14, Fraction{1, 8})));
            notes.push_back(inMeasure(3, pullOffAt(beat, Fraction{1, 2}, 3, 9, Fraction{1, 8})));
        }
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().place == Fraction{});
        CHECK(derived.shapes.front().sustain == Fraction{3});
        CHECK(derived.shapes.front().sounds_in_parts);
        // The posture is the chord's, unmoved: the tapping hand's fret 14 states nothing about it,
        // and the pull-offs restate the stop it already holds.
        REQUIRE(derived.shapes.front().posture < derived.postures.size());
        const ChartPosture& posture = derived.postures[derived.shapes.front().posture];
        CHECK(posture.stops[0] == std::optional{fretStop(5)});
        CHECK(posture.stops[1] == std::optional{fretStop(7)});
        CHECK(posture.stops[2] == std::optional{fretStop(9)});
    }
}

// THE THIRD WAY A TAP CAN WRITE AN EXTENT, and the one the family above does not see: not by
// bounding a span it is in, but by putting a string INTO one whose fretting ring is already over.
// The fold-in that carries a still-ringing string into a founding span is MEMBERSHIP, so it reads
// the reach column (`covers`, the fretting hand's own) and never `sounds`, which a tap writes. Read
// off `sounds`, the two disagree between the fretting ring's end and the tap's, and a span founded
// in that window takes in a member whose reach ends AT OR BEFORE the span's own front: the reach is
// the minimum of its members' coverage, so the front and the close land on one instant — a
// ZERO-LENGTH span with a sounding member in it (negative, where another string dates the front
// later), holding a grip (fret 5 here) that stopped sounding a quarter earlier. Shape rule 3 says a
// span with a sounding member is strictly positive, so that state is illegal on its face.
TEST_CASE("A tap's ring never makes a dead string a member", "[core][chart]")
{
    // Two picked notes a quarter long, then a tap on the first string over a second pair entering
    // an eighth apart. The tap's ring carries string 1's `sounds` to beat 3 while its fretting
    // ring ended at beat 2, so at the string-3 note on beat 2.5 string 1 is "still sounding" and
    // yet no member: its fret 5 has been dead for an eighth.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        tapAt(2, Fraction{}, 1, 12, Fraction{1, 4}),
        noteAt(2, Fraction{}, 2, 9, Fraction{1, 4}),
        noteAt(2, Fraction{1, 2}, 3, 9, Fraction{1, 4}),
    });
    const ChartShapes derived = deriveFrom(notes);
    everySpanIsPositive(derived);

    // ONE span, the opening pair's own. Without string 1's dead carry the beat-2 figure has a
    // struck member and one carried ring between them — two, short of the accumulation minimum —
    // so it founds nothing, exactly as any two notes under a tap found nothing.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().place == Fraction{});
    CHECK(derived.shapes.front().sustain == Fraction{1, 4});
    CHECK(derivedStops(derived, 0)[0] == std::optional{fretStop(5)});
    CHECK(derivedStops(derived, 0)[1] == std::optional{fretStop(7)});
    CHECK(memberCount(derived, 0) == 2);
}

// The other half of the same law, and the half that decides WHICH ONSETS the chain is continuous
// through: the adjacency set is every SOUNDING onset, whichever hand made it. The warrant is
// what happens to the member's tail at a tap — the tap ends it underneath, with no hand lifting
// anywhere, so the sound was REPLACED rather than silenced, and detachment is a statement about
// sound stopping. A tap therefore chains a statement it may not bound.
//
// This is the case the extent probes above deliberately cannot see: every one of them answers the
// same whether the set holds taps or not, because their taps land where the statement was ending
// anyway. Here the tap sits in the MIDDLE of the figure and a fretting-hand re-pick follows it, so
// the set alone decides whether this is one statement or a shape that died at the first tap.
TEST_CASE("Chart shape derivation chains a statement through a tap on a member", "[core][chart]")
{
    // The two-hand run over a held statement. String 1 states fret 5 and rings a quarter into a TAP
    // at a different fret (beat 2, ringing into beat 3) — the tapping hand taking over the string
    // the fretting hand is still holding — and the fretting hand then re-picks its own fret 5 at
    // beat 3, while string 2 rings through the whole figure as a member.
    //
    // The tap's sound bridges: string 1 never goes quiet, so the statement is in force at the
    // re-pick — which is exactly what lets the CHARACTER SPLIT land there: the re-pick sounds one
    // of the shape's two stops and breaks the box. The break lands past the statement's whole reach
    // (string 1's own ring ran out at the tap, and the tap's sound is not the fretting statement),
    // so the box REACH-CLOSES at a quarter, and the re-pick with the one surviving ring states
    // nothing further. The tap itself splits nothing — it states no stop, so its own slot asks no
    // character question — and its fret 12 is nowhere in the posture: audible to the chain,
    // invisible to the shape.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{3, 4}),
        tapAt(2, Fraction{}, 1, 12, Fraction{1, 4}),
        noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
    });
    const ChartShapes derived = deriveFrom(notes);
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().place == Fraction{});
    CHECK(derived.shapes.front().sustain == Fraction{1, 4});
    REQUIRE(derived.postures.size() == 1);
    CHECK(derived.postures.front().stops[0] == std::optional{fretStop(5)});
    CHECK(derived.postures.front().stops[1] == std::optional{fretStop(7)});
}

// A CHANGE IN ARTICULATION DOES NOT SPLIT THE SPAN (shape rule 2). The span is a fretting-hand
// statement, so the same frets played with a different technique restate it: one span, one
// posture.
TEST_CASE("Chart shape derivation rides a span through an articulation change", "[core][chart]")
{
    std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 8}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 8}),
        noteAt(2, Fraction{1, 2}, 3, 7, Fraction{1, 8}),
    };
    notes[2].palm_mute = true;
    notes[3].palm_mute = true;
    const ChartShapes derived = deriveFrom(notes);

    // ONE span over both strums — the palm-muted restrike states the same strings at the same
    // stops, and its ring is adjacent to the one before it — closing at the lone note that ends it.
    // The posture table deduplicates by frets alone: the technique is no part of a posture.
    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().place == Fraction{});
    CHECK(derived.shapes.front().sustain == Fraction{3, 8});
    CHECK(derived.shapes.front().posture == 0);
    // Both strums sounded the shape WHOLE, so the class is untouched too (shape rule 5): this is a
    // chord box that happens to change gesture, not an arpeggio.
    CHECK_FALSE(derived.shapes.front().sounds_in_parts);
}

// THE CHUG FIGURE END TO END, which is what shape rule 2 exists for: a plain chord, dead chugs on
// the same grip, and the plain chord again are ONE hand fact and derive as ONE span. Census-scale —
// this is the population the corpus collapses by.
TEST_CASE("Chart shape derivation holds one span across a dead chug run", "[core][chart]")
{
    // Eight adjacent eighth-note strums of the same two-string grip: plain, six dead chugs, plain.
    std::vector<ChartNote> notes;
    for (int strum = 0; strum < 8; ++strum)
    {
        const Fraction offset{strum, 2};
        const bool chug = strum > 0 && strum < 7;
        ChartNote low = noteAt(1, offset, 1, 5, Fraction{1, 8});
        ChartNote high = noteAt(1, offset, 2, 7, Fraction{1, 8});
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
    CHECK(derived.shapes.front().place == Fraction{});
    // Strike into strike the whole way, through the last chug's own ring: beat 1 to beat 5.
    CHECK(derived.shapes.front().sustain == Fraction{1});
    CHECK_FALSE(derived.shapes.front().sounds_in_parts);
}

// The derivation runs inside `normalizeChart`, which the load path deliberately runs BEFORE
// `validateChartRules` — so it is handed strings no rule has bounded yet, and it must not let one
// of them decide anything. It does not, because the posture width is the model's own string bound
// rather than a quantity read off the stream: a note on an impossible string takes no part in any
// posture, and the validator then refuses the document a moment later.
TEST_CASE("Chart shape derivation survives an unvalidated string number", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(1, Fraction{}, 1000000, 3, Fraction{1, 4}),
    };
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.postures.front().stops.size() == static_cast<std::size_t>(g_max_chart_strings));
    CHECK(derived.postures.front().stops[0] == std::optional{fretStop(5)});
    CHECK(derived.postures.front().stops[1] == std::optional{fretStop(7)});
    CHECK(derived.shapes.size() == 1);
}

// What the lifecycle doc names under "Posture and shape derivation": every reader derives from the
// SETTLED stream. Under shape rule 2 the settle cannot CHANGE this answer, and that is the point
// worth pinning — an articulation the rules refuse (a connection claim nothing justifies, here)
// never splits a span the chart's own notes say is one, because no articulation does.
TEST_CASE(
    "Chart shape derivation is unmoved by an articulation the settle rewrites", "[core][chart]")
{
    // Two identical strums, the second attacked as a connection on equal frets — which justifies
    // nothing, so the settle sweep records it as the plain pick it plays as.
    std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
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
    const ChartResolutions resolutions =
        chartResolutions(chart.notes, chart.shape_holds, chart.tuning.capo, tempo_map);
    REQUIRE(resolutions.shapes.size() == 1);
    CHECK(resolutions.shapes.front().place == Fraction{});
    CHECK(resolutions.shapes.front().sustain == Fraction{1, 2});
}

// Shape rule 1's carried member: a note still ringing across a chord's onset, on a string the strum
// does not re-strike, joins the posture — the hand is still holding it.
TEST_CASE("Chart shape derivation folds a ringing string into the posture", "[core][chart]")
{
    const std::vector<ChartNote> notes{
        noteAt(1, Fraction{}, 3, 7, Fraction{1, 2}),
        noteAt(2, Fraction{}, 1, 3, Fraction{1, 32}),
        noteAt(2, Fraction{}, 2, 5, Fraction{1, 32}),
    };
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.postures.size() == 1);
    REQUIRE(derived.postures.front().stops.size() >= 3);
    CHECK(derived.postures.front().stops[0] == std::optional{fretStop(3)});
    CHECK(derived.postures.front().stops[1] == std::optional{fretStop(5)});
    // The un-struck third string is part of the posture because it is still sounding.
    CHECK(derived.postures.front().stops[2] == std::optional{fretStop(7)});

    REQUIRE(derived.shapes.size() == 1);
    // THE DATING RULE: the span dates from its earliest member onset not covered by a preceding
    // span, and the ringing string's own onset is that. Nothing precedes it, so the FRONT is beat
    // one and the strum arrives inside the statement rather than opening it — not beat two, which
    // is merely the slot the walk NOTICES the shape at.
    CHECK(derived.shapes.front().place == Fraction{});
}

// Tap-only onsets are transparent to the grouping: they neither form a posture nor close a held
// one above its stops, so a chord ringing under two-hand tapping keeps its span.
TEST_CASE("Chart shape derivation rings a span through tap-only onsets", "[core][chart]")
{
    const auto chord_with_taps = [](const Fraction ring) {
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 3, ring),
            noteAt(1, Fraction{}, 2, 5, ring),
            noteAt(2, Fraction{}, 5, 12, Fraction{1, 8}),
            noteAt(2, Fraction{1, 2}, 6, 14, Fraction{1, 8}),
        };
        notes[2].attack = NoteAttack::Tap;
        notes[3].attack = NoteAttack::Tap;
        return notes;
    };

    SECTION("a held chord's span covers the taps")
    {
        const ChartShapes derived = deriveFrom(chord_with_taps(Fraction{1, 2}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().place == Fraction{});
        // The taps neither close nor trim it: the span runs the chord's whole ring.
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});
    }

    SECTION("a short-ringing chord's span still ends at its own ring, before the taps")
    {
        const ChartShapes derived = deriveFrom(chord_with_taps(Fraction{1, 4}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1, 4});
    }

    SECTION("one fretting-hand note under simultaneous taps derives no chord")
    {
        // The two-hand-tapping staple: a left-hand tap struck together with two right-hand taps.
        // The taps are invisible, so the onset counts one fretting-hand member — an ordinary
        // single onset, no posture, no span.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 9, Fraction{1, 32}),
            noteAt(1, Fraction{}, 5, 12, Fraction{1, 32}),
            noteAt(1, Fraction{}, 6, 14, Fraction{1, 32}),
            noteAt(1, Fraction{1, 8}, 3, 11, Fraction{1, 32}),
        };
        notes[0].attack = NoteAttack::LeftTap;
        notes[1].attack = NoteAttack::Tap;
        notes[2].attack = NoteAttack::Tap;
        const ChartShapes derived = deriveFrom(notes);
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
    }
}

// Side ruling (ii), NARROWED by THE CONTINUITY LAW (shape rule 3): a lone re-pick of a string the
// open span already holds keeps the span while the span's own statement is still in force. This is
// the one-note-at-a-time broken chord over a held shape, and it is DERIVED — nothing here is
// authored.
//
// The narrowing leaves no drawn-ring witness in the rule: an ADJACENT re-pick is continuity
// itself, and a GAP re-pick arrives after the statement has already ended. Two sections name the
// divergence from a witness reading in each direction, so the narrowing is tested rather than
// merely assumed harmless.
TEST_CASE("Chart shape derivation rides a span through a lone re-pick", "[core][chart]")
{
    // A three-string chord ringing two whole quarters, then one of its own members picked again
    // alone. Three strings, since a derived span of fewer than three members never turns to parts.
    const auto chord_then_repick = [](const Fraction chord_ring) {
        return std::vector<ChartNote>{
            noteAt(1, Fraction{}, 1, 5, chord_ring),
            noteAt(1, Fraction{}, 2, 7, chord_ring),
            noteAt(1, Fraction{}, 3, 7, chord_ring),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
        };
    };

    SECTION("the span runs THROUGH the re-pick, which absorbs the strum that founded it")
    {
        // THE ABSORPTION RULE. The strum does not stand alone: the very next slot picks one of its
        // own stops while its other member rings on, so it is ABSORBED — no boundary, no box span
        // — and the one statement flows to the rings' end, classed by the parts that sound inside
        // it. DISPLAY is untouched: the strum is two notes at one instant and wears its full box,
        // inside the bracket. The continuity half of side ruling (ii) holds underneath: the stored
        // rings are why the statement is in force at the re-pick at all, which is what lets it be
        // absorbed rather than arrive after a gap.
        const ChartShapes derived = deriveFrom(chord_then_repick(Fraction{1, 2}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});
        CHECK(derived.shapes.front().place == Fraction{});
        CHECK(derived.shapes.front().sounds_in_parts);
    }

    SECTION("a re-pick after a stored gap is an ordinary onset, so the span closes")
    {
        // The reason this is not "a lone note may always continue a span": the chord's rings stop
        // an eighth in with nothing restriking them, so the statement ended there and the beat-2
        // re-pick arrives after it. Under shape rule 2 it is just a note.
        const ChartShapes derived = deriveFrom(chord_then_repick(Fraction{1, 32}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().sustain == Fraction{1, 32});
    }

    SECTION("a gap on the re-picked string closes the span even while another member rings")
    {
        // The narrowing's own population, and the first divergence from a drawn-ring witness.
        // String 2 rings loudly through beat 2, so a WITNESS would say the hand had not left the
        // shape and would carry the span to two whole quarters. The law asks the re-picked string's
        // own stored ring instead: string 1 stopped an eighth in, which is an authored
        // detachment, so the statement ended there and this re-pick JOINS nothing.
        //
        // Nor does it found anything: the chord's three other strings still sound under it, but
        // they were struck behind the coverage frontier, and sound alone is asked only of
        // statements made since it (shape rule 1). A lone fresh note over old rings is no
        // accumulation, so a FOURTH chord string ringing through beside strings 2 and 3 changes
        // nothing: the rings ring out as plain tails.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{1, 2});
        notes[0].sustain = Fraction{1, 8};
        notes.push_back(noteAt(1, Fraction{}, 4, 11, Fraction{1, 2}));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
    }

    SECTION("an adjacent re-pick continues where no drawn tail could witness it")
    {
        // The divergence in the other direction, and the reason a witness is worse evidence than
        // the stored stream it would stand in for: string 2 is a dead click, so technique rule 25
        // takes its tail away in presentation and a witness would see NOTHING ringing — the span
        // would die at the margin before this re-pick (9/10 of a quarter) although the hand is
        // demonstrably still holding the shape. The stored ring says what the hand did: string 1
        // rings exactly into its own re-pick, string 2 rings through it, so every member is
        // continuous and the statement stands through both.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(1, Fraction{}, 3, 7, Fraction{1, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
        };
        notes[1].dead = true;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().place == Fraction{});
        // By the absorption rule the strum the re-pick picks into does not stand alone, so it
        // states no boundary and the one span runs through. The discrimination this section exists
        // for holds either way — a drawn-ring witness sees nothing ringing and would close at
        // the margin BEFORE the re-pick, while the stored rings prove the statement in force
        // through it.
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});
    }

    SECTION("a re-pick with a different articulation rides the span")
    {
        // Shape rule 2 asked of one string: the span is a fretting-hand statement, so the palm of
        // the OTHER hand cannot end it — the finger is still on fret 5.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{1, 2});
        notes[3].palm_mute = true;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        // The absorption rule carries the span through the re-pick either way; what this section
        // discriminates is that the OTHER hand's palm decides nothing — the palm-muted re-pick
        // lands exactly where the plain one does.
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});
    }

    SECTION("a re-pick at a DIFFERENT stop still closes the span")
    {
        // The control the section above needs: position is what the comparison reads, so moving the
        // finger is a contradiction the span cannot absorb — in EITHER founding (absorption admits
        // growth, never a stop the shape already states differently). Without this the section
        // above would pass on a rule that had stopped comparing anything. The moved finger then
        // founds nothing over the chord's rings still sounding beside it: they were struck behind
        // the break, so a lone fresh note over them is no accumulation (shape rule 1), and the
        // chord's span is the only one.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{1, 2});
        notes[3].fret = 6;
        notes.push_back(noteAt(1, Fraction{}, 4, 11, Fraction{1, 2}));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
    }

    SECTION("a lone strike on a string the shape never held GROWS the standing span")
    {
        // GROWTH CONTINUES: a stop the grip merely LACKS grows the span in place — the chord's
        // rings still sound, so the new stop unions with them and the statement runs on, ONE
        // statement from the strum's own onset.
        std::vector<ChartNote> notes = chord_then_repick(Fraction{1, 2});
        notes[3].string = 4;
        notes[3].fret = 9;
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        // Every one of the chord's still-ringing strings is a member beside the new stop.
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& grown =
            derived.postures[derived.shapes[0].posture].stops;
        CHECK(grown[0] == std::optional{fretStop(5)});
        CHECK(grown[1] == std::optional{fretStop(7)});
        CHECK(grown[2] == std::optional{fretStop(7)});
        CHECK(grown[3] == std::optional{fretStop(9)});
    }

    SECTION("a re-pick cannot OPEN a shape")
    {
        // Shape rule 1 is untouched: two sounding fretting-hand members or no posture at all.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
        };
        const ChartShapes derived = deriveFrom(notes);
        CHECK(derived.shapes.empty());
    }
}

// Shape rule 5's CLASS rule asked of a span's INTERIOR: ARPEGGIO iff the shape's members sound
// SEPARATELY, so a span stays a box chain only while every sounding of it is the shape WHOLE. The
// two figures the interior arm covers are one fact at two widths — a partial restrike and a lone
// re-pick of a single member — which is why they need no clause each.
//
// Two negatives carry the rule's edges, and both are needed. Shape rule 2 is unchanged underneath:
// a partial restrike with NO ring behind it is interior to nothing, because it split into a span of
// its own. And an interior slot that restates the shape WHOLE is the box chain the repeat marks are
// for, which is what keeps this from collapsing into "any span with an interior onset".
// Shape rules 3 and 5: a derived span of fewer than three members never turns to parts. The One
// Last Breath figure: open string 3 and string 6 fret 2 struck together, string 3 re-picked half a
// beat later while string 6 still rings. The two-note grip stays a box and the re-pick breaks it
// there; over three strings the same re-pick turns the span to an arpeggio.
TEST_CASE(
    "A derived span of fewer than three members closes at a partial sounding", "[core][chart]")
{
    SECTION("a two-note strike closes at the lone re-pick, a box")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 0, Fraction{1, 8}),
            noteAt(1, Fraction{}, 6, 2, Fraction{1, 4}),
            noteAt(1, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 8}});
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        CHECK(arpeggiosFrom(notes) == std::vector<bool>{false});
    }

    SECTION("the same re-pick under a three-note grip turns it to an arpeggio")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 0, Fraction{1, 8}),
            noteAt(1, Fraction{}, 5, 2, Fraction{1, 4}),
            noteAt(1, Fraction{}, 6, 2, Fraction{1, 4}),
            noteAt(1, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(arpeggiosFrom(notes) == std::vector<bool>{true});
    }

    SECTION("a tap over a two-note grip leaves it a box; over three strings it is an arpeggio")
    {
        const auto held_under_tap = [](const int strings) {
            std::vector<ChartNote> notes;
            for (int string = 1; string <= strings; ++string)
            {
                notes.push_back(noteAt(1, Fraction{}, string, 5, Fraction{1, 2}));
            }
            notes.push_back(tapAt(2, Fraction{}, 5, 12, Fraction{1, 8}));
            return streamOf(std::move(notes));
        };
        REQUIRE(deriveFrom(held_under_tap(2)).shapes.size() == 1);
        CHECK(arpeggiosFrom(held_under_tap(2)) == std::vector<bool>{false});
        REQUIRE(deriveFrom(held_under_tap(3)).shapes.size() == 1);
        CHECK(arpeggiosFrom(held_under_tap(3)) == std::vector<bool>{true});
    }
}

TEST_CASE("Chart shape arrival brackets a span its members sound in parts", "[core][chart]")
{
    // A three-string shape struck whole, then two of its three struck again a quarter later. What
    // the carried ring decides is whether the second strum is INSIDE the first's statement at all.
    const auto strum_then_partial = [](const Fraction carried_ring) {
        return std::vector<ChartNote>{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(1, Fraction{}, 3, 9, carried_ring),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
        };
    };

    SECTION("a partial restrike inside a ringing span absorbs the strum, and one bracket flows")
    {
        // THE ABSORPTION RULE, and the flagship figure: one span, classed by the parts, with no
        // seam at all. String 3 rings THROUGH the restrike, so the strum is not standing alone
        // when the partial sounds two of its three stops: it is ABSORBED. The strum keeps its box
        // as DISPLAY, drawn inside the bracket.
        const std::vector<ChartNote> notes = strum_then_partial(Fraction{1, 2});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio[0]);
    }

    SECTION("with no ring behind it the partial restrike splits, and both spans stay boxes")
    {
        // Shape rule 2 unchanged, and the reason the interior arm can never reach this: string 3's
        // ring ends exactly AT the restrike, so nothing folds in, the articulations differ, and the
        // smaller strum states a shape of its own. Neither span sounds anything but itself whole.
        const std::vector<ChartNote> notes = strum_then_partial(Fraction{1, 4});
        REQUIRE(deriveFrom(notes).shapes.size() == 2);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK_FALSE(arpeggio[1]);
    }

    SECTION("a lone re-pick is the same fact at one string's width")
    {
        // THE ABSORPTION RULE: the lone re-pick sounds ONE of the shape's three members while the
        // others ring, so the strum does not stand alone and is absorbed. The class is decided AT
        // the strum, off the strum's own rings and the one slot that follows them — never
        // retroactively — and the span has no seam.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(1, Fraction{}, 3, 7, Fraction{1, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
        };
        REQUIRE(deriveFrom(notes).shapes.size() == 1);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());
    }

    SECTION("a re-pick with a chord crowding in behind it seams at the CONTRADICTION")
    {
        // THE ABSORPTION RULE: the re-pick absorbs the strum instead of seaming it, so the seam
        // this section watches sits on the crowding chord a sixteenth later, which is where the
        // grip actually MOVES — fret 3 on a string the span states at 5 is a contradiction, and
        // that is what ends the statement. The span's own last STATEMENT is the re-pick, not its
        // front, which is what the stated extent reads.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{5, 16}),
            noteAt(1, Fraction{}, 3, 7, Fraction{5, 16}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 16}),
            noteAt(2, Fraction{1, 4}, 1, 3, Fraction{1, 4}),
            noteAt(2, Fraction{1, 4}, 2, 3, Fraction{1, 4}),
        };
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes.front().sustain == Fraction{5, 16});
        CHECK(derived.shapes.front().stated_extent == Fraction{1, 4});
        CHECK(derived.shapes[1].place == Fraction{5, 16});
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK(arpeggio.front());
    }

    SECTION("a chug chain restating the shape whole stays a box")
    {
        // The interior arm's own discriminating negative: one span, two interior slots, and each of
        // them sounds the shape WHOLE. This is what the repeat boxes are drawn over.
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1, 4}),
        };
        REQUIRE(deriveFrom(notes).shapes.size() == 1);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK_FALSE(arpeggio.front());
    }
}

// CLASSIFICATION READS THE STORED STREAM.
//
// One figure, two arms of the one class law. A DEAD string carries into a chord's onset: its
// STORED ring is real timing, its DRAWN tail is gone (technique rule 25). The walk's fold-in asks
// the stored ring, so the carry joins the posture and an interior restrike classifies the span as
// an arpeggio; re-deriving that same carry off the DRAWN tail at the span's start would find
// nothing there — same figure, same law, two answers, decided by which slot you happened to look
// at.
//
// The stored reading is the only one, so the two arms are the same comparison. Both sections below
// assert ONE answer.
TEST_CASE("A dead string's carry classifies at a span start and inside it alike", "[core][chart]")
{
    // String 3 is struck DEAD at beat one and rings three quarters in the stored stream; the chord
    // on strings 1 and 2 lands a quarter later, inside that ring.
    const auto dead_carry = [](const bool restrike) {
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 9, Fraction{3, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 2}),
        };
        notes.front().dead = true;
        if (restrike)
        {
            notes.push_back(noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}));
            notes.push_back(noteAt(3, Fraction{}, 2, 7, Fraction{1, 4}));
        }
        return streamOf(notes);
    };

    SECTION("the START arm: the carry alone brackets the span")
    {
        // Nothing restrikes, so the only slot that says anything is the span's own start — where
        // two of the shape's three sounding strings arrive and the third was already down. This is
        // the arm a drawn-tail reading would answer BOX for, because the dead note presents no
        // tail.
        const std::vector<ChartNote> notes = dead_carry(false);
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        // The dead string joined the posture, which is what a carry means and what technique
        // rule 25 never touched: presentation takes the drawn tail, not the fact that the finger
        // is down.
        REQUIRE(derived.shapes.front().posture < derived.postures.size());
        CHECK(
            derived.postures[derived.shapes.front().posture].stops[2] ==
            std::optional{fretStop(9)});
        CHECK(derived.shapes.front().sounds_in_parts);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 1);
        CHECK(arpeggio.front());
    }

    SECTION("the INTERIOR arm agrees, on the same figure")
    {
        // The same carry, plus a restrike of the two struck members a quarter later. That slot
        // sounds two of three as well, so the interior arm answers exactly what the start arm just
        // did — one comparison, one span, one answer, whichever slot asks.
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
        // The discriminating negative, and the reason the rule is not "dead notes are arpeggios":
        // nothing is CARRIED here. The dead string is struck with the others, so every member of
        // the shape sounds at the one slot and the box stands.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1, 4}),
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
}

// Two different postures each get their own entry, and a stream with nothing struck together
// derives nothing at all.
TEST_CASE("Chart shape derivation tables one posture per distinct fret vector", "[core][chart]")
{
    SECTION("distinct fret vectors are distinct postures")
    {
        const std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 3, Fraction{1, 32}),
            noteAt(1, Fraction{}, 2, 5, Fraction{1, 32}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 32}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 32}),
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
            noteAt(1, Fraction{}, 1, 3, Fraction{1, 32}),
            noteAt(2, Fraction{}, 2, 5, Fraction{1, 32}),
            noteAt(3, Fraction{}, 3, 7, Fraction{1, 32}),
        };
        const ChartShapes derived = deriveFrom(notes);
        CHECK(derived.shapes.empty());
        CHECK(derived.postures.empty());
    }
}

// A TAP STATES NOTHING TO THE SPANS: its plant is a fact the pull-off's own sounding note restates
// when it lands, so the grip the spans print is read off the notes that sound and nothing the tap
// claims. The figure is the one corner where a tap's claim could seem to matter: a two-string
// statement on the downbeat puts string ONE on the landing stop, then that span is broken in the
// tap's own slot by string two moving. The successor founds on the pull-off
// and the rings around it, and its posture still holds the landing stop — stated by the note that
// sounds it.
TEST_CASE("A tap's plant reaches the spans through the pull-off that states it", "[core][chart]")
{
    const auto figure = [](const std::optional<int> down, const int landed) {
        ChartNote pull = noteAt(2, Fraction{1, 2}, 1, landed, Fraction{3, 8});
        pull.attack = NoteAttack::Legato;
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(2, Fraction{}, 2, 9, Fraction{1, 2}),
            noteAt(2, Fraction{1, 2}, 3, 9, Fraction{3, 8}),
            tapAt(2, Fraction{}, 1, 12, Fraction{1, 8}),
            pull,
        };
        if (down.has_value())
        {
            notes.push_back(noteAt(1, Fraction{}, 1, *down, Fraction{1, 4}));
        }
        return streamOf(std::move(notes));
    };

    SECTION("the landing stop is in the successor's posture, stated by the note that sounds it")
    {
        const std::vector<ChartNote> stream = figure(5, 5);
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        CHECK(derivedStops(derived, 1)[0] == std::optional{fretStop(5)});
        CHECK(derivedStops(derived, 1)[1] == std::optional{fretStop(9)});
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(9)});
        everySpanIsPositive(derived);
    }

    SECTION("with no grip standing, the released note states its stop just the same")
    {
        const std::vector<ChartNote> stream = figure(std::nullopt, 5);
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derivedStops(derived, 0)[0] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }

    SECTION("a HAMMER splits the string into two statements")
    {
        // The connection runs UP from the tap, so the downbeat's 5 and the hammered 15 are two
        // statements of one string and the figure is two spans.
        const std::vector<ChartNote> stream = figure(5, 15);
        const ChartShapes derived = deriveFrom(stream);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derivedStops(derived, 0)[0] == std::optional{fretStop(5)});
        CHECK(derivedStops(derived, 1)[0] == std::optional{fretStop(15)});
        everySpanIsPositive(derived);
    }
}

// TRAVEL SPLITS AT THE LANDING, AND THE LANDED GRIP RE-OPENS THERE (shape rule 4). A chord slide
// keeps the fingers planted, so the rings run continuously and the span COVERS the transit, ending
// where the new grip is established — which is exactly where the successor opens, so the two TILE.
// The members' sliding tails draw across the transit, and the successor draws no opening mark of
// its own: the continued tails and the chord NAME changing at the landing are the whole statement.
TEST_CASE("Chart shape derivation splits a span at a member's travel", "[core][chart]")
{
    SECTION("a chord slide states its departing grip, then the grip it lands in")
    {
        // All three members hold their stop through a restating keyframe at a quarter, then travel
        // and come to rest at two quarters. Shape rule 4: the box covers the hold AND the transit,
        // because the fingers stay planted through a chord slide and the rings run continuously —
        // the split falls at the LANDING, where the new grip is established, and the two spans TILE
        // with no gap between them.
        //
        // THREE strings here and in most glides below, which under shape rule 4 is a choice of
        // figure rather than a requirement: A LANDING OPENS AT TWO. Its members were already
        // established members of the span that just closed, so nothing arrives staggered at a
        // landing and the three-member minimum — which gates FOUNDING BY SOUND ALONE and nothing
        // else — never applies to one. The two-string glide is pinned with shape rule 4's refusal,
        // in "a slot inside the travel closes the span" below.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{1}),
                {{Fraction{1, 4}, 5}, {Fraction{1, 2}, 7}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{1}),
                {{Fraction{1, 4}, 7}, {Fraction{1, 2}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{1}),
                {{Fraction{1, 4}, 9}, {Fraction{1, 2}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        // The successor sits at the landing, which is no note slot at all — the one span in the
        // model that opens where nothing is struck and nothing is claimed. It opens exactly where
        // its predecessor closes, which is what TILING means here.
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});

        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& departed =
            derived.postures[derived.shapes[0].posture].stops;
        CHECK(departed[0] == std::optional{fretStop(5)});
        CHECK(departed[1] == std::optional{fretStop(7)});
        CHECK(departed[2] == std::optional{fretStop(9)});
        // Bracket digits stating the LANDED grip, which is the whole reason the successor exists.
        const std::vector<std::optional<ChartStop>>& landed =
            derived.postures[derived.shapes[1].posture].stops;
        CHECK(landed[0] == std::optional{fretStop(7)});
        CHECK(landed[1] == std::optional{fretStop(9)});
        CHECK(landed[2] == std::optional{fretStop(11)});

        // The class, end to end: BOX at both ends. The departing shape was struck whole; nothing
        // strikes the successor at all — a LANDING IS NOT A SOUNDING, so no trigger fires there,
        // and the published picture is two boxes joined by the members' sliding tails.
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK_FALSE(arpeggio[1]);
        // Nothing is claimed here and nothing sounds inside the successor, so every trigger is
        // silent — the class is a box because no rule made it anything else.
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
    }

    SECTION("a travel from the very first fret statement still covers its own transit")
    {
        // No restating keyframe: the channel's first statement after the onset already names a
        // different stop, so the hand departs AT the strike. A departure split would give this span
        // no length at all, which is worse than covering the transient state; under the landing
        // split the whole glide is the departing grip's span, and no zero-length span exists in the
        // model to draw.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{1, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{1, 2}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
    }

    SECTION("edge (a): one member travelling re-opens the voicing it lands in")
    {
        // The ratified symmetry: a lone sliding finger under held ones still lands the hand in a
        // different chord, so the successor states it. What stayed put is INHERITED at its own
        // stop — the fingers slid, they never lifted, so the landed grip carries them.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& landed =
            derived.postures[derived.shapes[1].posture].stops;
        CHECK(landed[0] == std::optional{fretStop(7)});
        CHECK(landed[1] == std::optional{fretStop(7)});
        CHECK(landed[2] == std::optional{fretStop(9)});
    }

    SECTION("edge (b): a travel landing into a FOREIGN chord opens no successor")
    {
        // The chart's own encoding of "glides into that note": the arrival sits exactly one
        // minimum sustain distance before the landing's onset, and the ring ends at that onset.
        // The landed grip therefore has no moment of its own, and the strike's own full box states
        // the new chord.
        //
        // Under tiling this is no test of its own (shape rule 4): the successor opens at the
        // landing and the next statement closes it one margin later, leaving it no room to be DRAWN
        // in — and a span no EVENT states, with no room of its own, states nothing and is not
        // emitted. This is the ONE derivation question that reads the display margin, every other
        // use of it living at the projection, and it does so on purpose: whether the landed grip
        // gets a moment of its own is whether a reader could see one. The margin is taken at the
        // CLOSING onset, as every other reader takes it and never at the arrival; the cross-tempo
        // section below is what pins that.
        //
        // The closing statement has to be a FOREIGN one (shape rule 4). A restrike of the LANDED
        // grip does not close the successor at all — it restates it, so it rides inside it, which
        // the section below pins.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}), {{Fraction{19, 40}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}), {{Fraction{19, 40}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1, 2}), {{Fraction{19, 40}, 11}}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        // The departing grip covers its own glide, ending exactly at the landing it never gets to
        // state.
        CHECK(derived.shapes[0].sustain == Fraction{19, 40});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[1]);

        // The discrimination, one field apart: the same glide with a ring that BREATHES past its
        // landing does re-open, because there the grip is heard on its own.
        const std::vector<ChartNote> breathing = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{3, 4}), {{Fraction{19, 40}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{3, 4}), {{Fraction{19, 40}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{3, 4}), {{Fraction{19, 40}, 11}}),
        });
        const ChartShapes landed = deriveFrom(breathing);
        REQUIRE(landed.shapes.size() == 2);
        CHECK(landed.shapes[1].place == Fraction{19, 40});
        CHECK(landed.shapes[1].sustain == Fraction{11, 40});
    }

    SECTION("THE LANDING PIN: a landing met by a restrike emits the departing grip and the chord")
    {
        // The pin for the boundary rule: the continuity law alone judges a death, and a
        // LANDING is the case a second reading of the same adjacency could part from it over, since
        // a landing is a reach that is not a ring's end.
        //
        // They do not part. This figure is a CHARTER's own arrival, standing one margin inside its
        // ring with the ribbon running on to a FOREIGN chord. The emitted figure is the plain one:
        // the departing grip covers its transit and ends at the landing, the successor is left no
        // room and is never emitted (its tenure is exactly one quantum, which the strict test
        // drops), and the restrike's own statement stands on its own. A SHIFT SLIDE reaches none of
        // this, which the case below pins.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}), {{Fraction{19, 40}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}), {{Fraction{19, 40}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1, 2}), {{Fraction{19, 40}, 11}}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // TWO spans and no third: the departing grip, and the chord that replaced it. The landed
        // grip's own successor is opened — its survivors reach shape rule 4's threshold — and its
        // tenure is exactly one quantum, which the STRICT test drops, so it is never emitted. That
        // is what the absence of any landing-opened span below states.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{19, 40});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        CHECK(std::ranges::none_of(derived.shapes, [](const ChartShape& shape) {
            return shape.landing_opened;
        }));
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& departing =
            derived.postures[derived.shapes[0].posture].stops;
        CHECK(departing[0] == std::optional{fretStop(5)});
        CHECK(departing[1] == std::optional{fretStop(7)});
        CHECK(departing[2] == std::optional{fretStop(9)});
        const std::vector<std::optional<ChartStop>>& replacing =
            derived.postures[derived.shapes[1].posture].stops;
        CHECK(replacing[0] == std::optional{fretStop(3)});
        CHECK(replacing[1] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }

    SECTION("A SHIFT SLIDE OPENS NO LANDING AT ALL, by construction")
    {
        // The same three-string glide as a SHIFT SLIDE: the arrival stands AT each ring's end, on
        // the re-picked chord's own onset, and the chart PROVES it is an arrival because every fret
        // it names is the stop that chord is struck at (\ref arrivesIntoNextHead). A landing hands
        // the grip over only where the ring runs STRICTLY PAST it, and an arrival never does — so
        // no successor is opened, shape rule 4's tenure test is never reached, and the departing
        // grip simply runs to the chord that replaces it.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}), {{Fraction{1, 2}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}), {{Fraction{1, 2}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1, 2}), {{Fraction{1, 2}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 4}),
            noteAt(3, Fraction{}, 3, 11, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(std::ranges::none_of(derived.shapes, [](const ChartShape& shape) {
            return shape.landing_opened;
        }));
        // The departing grip covers its whole transit, ending ON the chord it slides into: the
        // store says the hand arrives there, so the span says so too.
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        everySpanIsPositive(derived);
    }

    SECTION("a full restrike of the landed grip rides INSIDE the successor")
    {
        // Shape rule 4: a full restrike of a landed grip stays INSIDE the successor, and its full
        // box comes from the display law. The strike states the successor's own stops, so it
        // restates the shape like any other restrike.
        //
        // The suppression for want of room (edge (b)) applies to a FOREIGN chord alone — the
        // section above is that control.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}), {{Fraction{19, 40}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}), {{Fraction{19, 40}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1, 2}), {{Fraction{19, 40}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 4}),
            noteAt(3, Fraction{}, 3, 11, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{19, 40});
        // ONE successor, opened at the landing and carried through the restrike's own ring.
        CHECK(derived.shapes[1].place == Fraction{19, 40});
        CHECK(derived.shapes[1].sustain == Fraction{11, 40});
        CHECK(derived.shapes[1].landing_opened);
        // BOX CLASS END TO END: a chord sliding into chords is not an
        // arpeggio. The landing fires no trigger — a landing is not a sounding — and the restrike
        // inside the successor is the shape WHOLE, so nothing makes either span an arpeggio. What
        // draws is two boxes joined by the members' sliding tails, which is the published
        // chord-slide picture.
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK_FALSE(arpeggio[1]);
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
    }

    SECTION("an interior lone re-pick makes the successor an arpeggio")
    {
        // The other side of the same law: a successor is classified by what sounds INSIDE it, so a
        // sounding that reaches only part of the landed grip is shape rule 5's class rule at its
        // ordinary width. One string of the landed grip is re-picked at its own landed stop, which
        // rides the successor and states its members arriving separately. Every ring
        // runs past the re-pick, so the successor's statement is still in force there — a re-pick
        // AT the shortest member's end would close the span instead of riding it.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{7, 16}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{7, 16}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{7, 16}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].landing_opened);
        CHECK(derived.shapes[1].sounds_in_parts);
        const std::vector<bool> arpeggio = arpeggiosFrom(notes);
        REQUIRE(arpeggio.size() == 2);
        CHECK_FALSE(arpeggio[0]);
        CHECK(arpeggio[1]);
    }

    SECTION("edge (c): staggered landings open no successor")
    {
        // The members come to rest an eighth apart each, so no single instant states a grip; the
        // truth stays in the sliding tails (watch item, docs/tracking/watch-items.md). Three
        // members with three different landings, so the refusal is the STAGGERING and not the
        // member count — the control below lands the same three together and does re-open.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{5, 8}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{3, 4}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        // The EARLIEST landing ends the span (shape rule 4): coverage is the minimum of the
        // members' own, so the first finger to come to rest is where the shape stops being the one
        // that was travelling. The other fingers' remaining glide draws as their tails.
        CHECK(derived.shapes.front().sustain == Fraction{1, 2});

        // The control, one offset apart on two of them: landing together is what re-opens.
        const std::vector<ChartNote> together = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{1, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{1, 2}, 11}}),
        });
        CHECK(deriveFrom(together).shapes.size() == 2);
    }

    SECTION("THE PARTIALLY STAGGERED SLIDE: two arrivals open the pair, the third states nothing")
    {
        // Edge (c)'s refusal is carried by "fewer than TWO", never by "fewer than three" — THE
        // LANDING OPENS AT TWO (shape rule 4) — so the fully staggered figure above cannot
        // discriminate the case where SOME of the fingers arrive together. This is that case: two
        // members come to rest at the same instant and the third is still mid-travel there.
        //
        // Two arrivals are a landed grip, so the successor opens holding exactly those two — the
        // third finger is on NO stop at the boundary (a finger between stops is a member of
        // nothing), so it joins no posture. Its own later landing then arrives alone, which is one
        // member and states nothing at all.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{1, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{5, 8}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        // The departing grip, struck whole and covering its own transit to the earliest landing.
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& departing =
            derived.postures[derived.shapes[0].posture].stops;
        CHECK(departing[0] == std::optional{fretStop(5)});
        CHECK(departing[1] == std::optional{fretStop(7)});
        CHECK(departing[2] == std::optional{fretStop(9)});

        // The landed PAIR, and nothing of the finger still travelling.
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].landing_opened);
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& landed =
            derived.postures[derived.shapes[1].posture].stops;
        CHECK(landed[0] == std::optional{fretStop(7)});
        CHECK(landed[1] == std::optional{fretStop(9)});
        CHECK_FALSE(landed[2].has_value());
        everySpanIsPositive(derived);
    }

    SECTION("edge (d): unequal travels landing together state whatever grip landed")
    {
        // A voice-leading slide: the fingers move by different distances. Nothing in the rule asks
        // how far a finger went, only where it was last stated and where it comes to rest.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{1, 2}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{1, 2}, 12}}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& landed =
            derived.postures[derived.shapes[1].posture].stops;
        CHECK(landed[0] == std::optional{fretStop(7)});
        CHECK(landed[1] == std::optional{fretStop(8)});
        CHECK(landed[2] == std::optional{fretStop(12)});
    }

    SECTION("the successor runs by THE CONTINUITY LAW, ending at its first arrived gap")
    {
        // Its members are the arrived rings, so its extent is theirs: the shorter ring stops with
        // nothing sounding it, which is an authored detachment, and the successor closes there.
        // The strum after that gap founds on its own two strokes and dates from them (shape rule
        // 1), never stretching back into the successor; the arrived string 3 still sounding there
        // joins it as a carried member. That member dies with the strum's own rings, so the span
        // closes there whole rather than splitting.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{3, 4}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{5, 4}), {{Fraction{1, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{5, 4}), {{Fraction{1, 2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{1, 4})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 9, Fraction{1, 4})),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        CHECK(derived.shapes[2].place == Fraction{1});
        CHECK(derived.shapes[2].sustain == Fraction{1, 4});
        CHECK(memberCount(derived, 2) == 3);
        CHECK(derivedStops(derived, 2)[2] == std::optional{fretStop(11)});
        CHECK(derived.shapes[2].sounds_in_parts);
    }

    SECTION("a glide with a held grip between its legs states each grip once")
    {
        // Three statements of the hand, and the model's own reading of the channel is what tells
        // them apart: equal frets are a HOLD, so the restated stop at two quarters is a grip the
        // successor brackets, and the travel out of it splits that successor in turn.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{5, 4}),
                {{Fraction{1, 4}, 7}, {Fraction{1, 2}, 7}, {Fraction{3, 4}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{5, 4}),
                {{Fraction{1, 4}, 9}, {Fraction{1, 2}, 9}, {Fraction{3, 4}, 11}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{5, 4}),
                {{Fraction{1, 4}, 11}, {Fraction{1, 2}, 11}, {Fraction{3, 4}, 13}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        // Three statements, TILING end to end (shape rule 4): each covers its own hold and the
        // glide out of it, and the next opens exactly where the last closes.
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        CHECK(derived.shapes[2].place == Fraction{3, 4});
        CHECK(derived.shapes[2].sustain == Fraction{1, 2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& middle =
            derived.postures[derived.shapes[1].posture].stops;
        CHECK(middle[0] == std::optional{fretStop(7)});
        CHECK(middle[1] == std::optional{fretStop(9)});
        CHECK(middle[2] == std::optional{fretStop(11)});
    }

    SECTION("a continuous multi-fret glide brackets only where it comes to rest")
    {
        // The same three fret statements with the middle one never restated: the channel leaves it
        // again, so it is a point on the path and not a grip. One travel, one landing.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{1}),
                {{Fraction{1, 4}, 7}, {Fraction{1, 2}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{1}),
                {{Fraction{1, 4}, 9}, {Fraction{1, 2}, 11}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{1}),
                {{Fraction{1, 4}, 11}, {Fraction{1, 2}, 13}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        // One travel, so one covered transit: the departing grip runs to the only landing there
        // is, and the transit fret at a quarter gets no span of its own.
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& landed =
            derived.postures[derived.shapes[1].posture].stops;
        CHECK(landed[0] == std::optional{fretStop(9)});
        CHECK(landed[1] == std::optional{fretStop(11)});
        CHECK(landed[2] == std::optional{fretStop(13)});
    }

    SECTION("a glide that turns back lands on the fret it turns at")
    {
        // A chord with open strings whose two fretted members glide 5 to 7 and back to 5. The 7
        // is never restated, but the glide TURNS on it, so it is the chord landed on: three spans
        // tiling the ring, each landed grip keeping the open strings as members of the chord.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 0, Fraction{3, 8}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 5, Fraction{3, 8}),
                {{Fraction{1, 8}, 7}, {Fraction{1, 4}, 5}}),
            travellingAt(
                noteAt(1, Fraction{}, 4, 5, Fraction{3, 8}),
                {{Fraction{1, 8}, 7}, {Fraction{1, 4}, 5}}),
            noteAt(1, Fraction{}, 5, 0, Fraction{3, 8}),
            noteAt(1, Fraction{}, 6, 0, Fraction{3, 8}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        const auto chord_at = [](const int fretted) {
            std::vector<std::optional<ChartStop>> stops(
                static_cast<std::size_t>(g_max_chart_strings));
            stops[0] = fretStop(0);
            stops[1] = fretStop(fretted);
            stops[3] = fretStop(fretted);
            stops[4] = fretStop(0);
            stops[5] = fretStop(0);
            return stops;
        };
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        CHECK(derivedStops(derived, 0) == chord_at(5));
        CHECK(derived.shapes[1].place == Fraction{1, 8});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        CHECK(derivedStops(derived, 1) == chord_at(7));
        CHECK(derived.shapes[2].place == Fraction{1, 4});
        CHECK(derived.shapes[2].sustain == Fraction{1, 8});
        CHECK(derivedStops(derived, 2) == chord_at(5));
        // The shape was slid into, so neither landing states anything a bracket could show.
        for (const std::size_t landed : {std::size_t{1}, std::size_t{2}})
        {
            CHECK(derived.shapes[landed].landing_opened);
            CHECK_FALSE(derived.shapes[landed].bracket_place.has_value());
        }
        everySpanIsPositive(derived);
    }
}

// A landing that opens no successor (shape rule 4) still ends the span's statement: the landed
// finger states another stop, so the stop the span states is no longer in force at the slot struck
// there, and the span closes at that slot with its head. Here the open string's ring ends at the
// landing, leaving one survivor, and the open string is restruck exactly there.
TEST_CASE("A span closes at the slot where its member's glide lands alone", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 2, 0, Fraction{1, 8}),
        travellingAt(noteAt(1, Fraction{}, 3, 7, Fraction{1, 4}), {{Fraction{1, 8}, 5}}),
        noteAt(1, Fraction{1, 2}, 2, 0, Fraction{1, 8}),
    });
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sustain == Fraction{1, 8});
    CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 8}});
}

// Shape rule 4 — the split falls at the LANDING. What that BUYS, case by case: the transit rides
// the predecessor, so every slot inside a glide is inside a span; the two spans TILE; and no span
// with sound in it is ever emitted at zero length. Each section here is a hole a departure split
// would leave open, probed on a figure that derives the answer.
TEST_CASE("The landing split covers a travel and hands the grip over", "[core][chart]")
{
    // Three fingers hold a grip through a restating keyframe at a quarter, glide, and come to rest
    // at two quarters, ringing on to four. The departing span runs to the landing at two, and the
    // successor runs from there.
    //
    // THREE strings by choice of figure, not by requirement: shape rule 4 opens a landing at TWO
    // survivors, and the three-member minimum gates founding by sound alone and nothing else.
    const auto chord_slide = [] {
        return std::vector<ChartNote>{
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{1}),
                {{Fraction{1, 4}, 5}, {Fraction{1, 2}, 7}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{1}),
                {{Fraction{1, 4}, 7}, {Fraction{1, 2}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{1}),
                {{Fraction{1, 4}, 9}, {Fraction{1, 2}, 11}}),
        };
    };

    SECTION("a picking-hand onset inside the travel flips the class, at the start and mid-glide")
    {
        // Trigger (d) asks whether a right-hand onset sounds INSIDE the span. A departure split
        // would answer it against an extent that stopped at the departure — a tap at the very slot
        // the shape was struck at would fall outside its own span, which has no length to contain
        // it, and one taken mid-glide would fall in a span-free transit. Under the landing split
        // both are inside the covering span.
        const std::vector<ChartNote> plain = chord_slide();
        const std::vector<bool> untapped = arpeggiosFrom(streamOf(plain));
        REQUIRE(untapped.size() == 2);
        CHECK_FALSE(untapped[0]);

        std::vector<ChartNote> tapped_at_start = chord_slide();
        tapped_at_start.push_back(tapAt(1, Fraction{}, 5, 12, Fraction{1, 8}));
        const std::vector<bool> at_start = arpeggiosFrom(streamOf(tapped_at_start));
        REQUIRE(at_start.size() == 2);
        CHECK(at_start[0]);

        std::vector<ChartNote> tapped_mid_travel = chord_slide();
        tapped_mid_travel.push_back(tapAt(2, Fraction{1, 2}, 5, 12, Fraction{1, 8}));
        const std::vector<bool> mid_travel = arpeggiosFrom(streamOf(tapped_mid_travel));
        REQUIRE(mid_travel.size() == 2);
        CHECK(mid_travel[0]);
    }

    SECTION("open members restruck mid-slide ride the one span, per member and not per slot")
    {
        // The real-song figure: a shape with fretted AND open members slides, and the OPEN strings
        // are restruck while it travels. An open channel never departs, so those restrikes sound a
        // stop the shape STILL STATES — an interior subset sounding like any other. Judged per SLOT
        // the travelling members' silence would truncate the span at the first of them; judged per
        // MEMBER, which is what the shape actually states, the span rides through both restrikes
        // and splits only at the landing.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{1, 2}, 9}}),
            noteAt(1, Fraction{}, 3, 0, Fraction{1}),
            noteAt(1, Fraction{}, 4, 0, Fraction{1}),
            noteAt(2, Fraction{}, 3, 0, Fraction{3, 4}),
            noteAt(2, Fraction{}, 4, 0, Fraction{3, 4}),
            noteAt(2, Fraction{1, 2}, 3, 0, Fraction{5, 8}),
            noteAt(2, Fraction{1, 2}, 4, 0, Fraction{5, 8}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        // THE PARTIAL-SLIDE READING, ring-refined: the statement is divided only where a held
        // member's ring ends while a co-struck glide is still in flight. Here the open strings ring
        // the WHOLE transit — their statement never comes apart, and the mid-flight restrikes are
        // renewals the transit carries — so the span stays a box. The half this section exists for
        // stands: the span RIDES both restrikes per member and splits only at the landing.
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        CHECK_FALSE(arpeggiosFrom(notes)[0]);
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        everySpanIsPositive(derived);

        // The control, and the one statement a travelling span cannot absorb: a restrike naming
        // ANOTHER stop on a string the grip states is a CONTRADICTION (shape rule 3), so it
        // truncates the travel there and the landing, behind the close, opens nothing. It has to be
        // a contradiction, because a mid-slide sounding on a string the shape does not STATE would
        // be ordinary growth. The contradicting strike then founds nothing over the two open rings
        // still sounding: they were struck behind the close, and a lone fresh note over old rings
        // is no accumulation (shape rule 1). The other glide is mid-travel and states no stop
        // (shape rule 4), so the landing it makes hands nothing over either.
        const std::vector<ChartNote> contradicting = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{1, 2}, 9}}),
            noteAt(1, Fraction{}, 3, 0, Fraction{1}),
            noteAt(1, Fraction{}, 4, 0, Fraction{1}),
            noteAt(2, Fraction{}, 1, 3, Fraction{3, 4}),
        });
        const ChartShapes truncated = deriveFrom(contradicting);
        REQUIRE(truncated.shapes.size() == 1);
        CHECK(truncated.shapes[0].sustain == Fraction{1, 4});
        everySpanIsPositive(truncated);
    }

    SECTION("a lone re-pick of a landed member rides the successor")
    {
        // The successor's members are RINGS it never struck, so it has no strike
        // articulation for a re-pick to match — the same shape a claimed member has, and the
        // same answer: the STOP is the whole test. Matching the carried record whole would be one
        // comparison doing two rules' work, and would refuse the lone re-pick that rides every
        // other span in the model.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{3, 2}), {{Fraction{1, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{3, 2}), {{Fraction{1, 2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{1, 2})),
        });
        const ChartShapes derived = deriveFrom(notes);

        // Two spans, and the successor runs its full four quarters: the re-pick RIDES it, so the
        // re-picked string's own new ring carries the statement on. Closing the successor at the
        // re-pick instead would give a bracket of seven sixteenths, with the re-picked note left
        // inside no span at all.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(derived.shapes[1].sustain != Fraction{7, 16});
        CHECK(derived.shapes[1].sounds_in_parts);

        // One string apart, a FULL restatement of the landed grip RIDES the successor too (shape
        // rule 4): the re-pick and the full restrike are one law, which is what the separation is
        // for.
        const std::vector<ChartNote> restruck = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{3, 2}), {{Fraction{1, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{3, 2}), {{Fraction{1, 2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{1, 2})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 9, Fraction{1, 2})),
            inMeasure(2, noteAt(1, Fraction{}, 3, 11, Fraction{1, 2})),
        });
        const ChartShapes fresh = deriveFrom(restruck);
        REQUIRE(fresh.shapes.size() == 2);
        CHECK(fresh.shapes[1].place == Fraction{1, 2});
        CHECK(fresh.shapes[1].landing_opened);
        everySpanIsPositive(fresh);
    }

    SECTION("only the landing successor is published as landing-opened")
    {
        // Shape rule 4's one derivational fact for a landing, and display keys the whole bracket
        // deferral on it. What makes it a field rather than a test a reader could run for itself is
        // that every available proxy drifts: a successor states nothing at its own start, but so
        // does a span the hand alone opened — and the walk's own record of "an event stated this"
        // stops being empty the moment an interior re-pick states the successor, which is exactly
        // the case the deferral has to survive.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{3, 2}), {{Fraction{1, 2}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{3, 2}), {{Fraction{1, 2}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 7, Fraction{1, 2})),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK_FALSE(derived.shapes[0].landing_opened);
        CHECK(derived.shapes[1].landing_opened);
        // And the re-pick that rides the successor does not clear it: the span is still the span
        // the landing opened, however many statements land inside it afterwards.
        CHECK(derived.shapes[1].sustain == Fraction{1});
    }

    SECTION("the span the glide covers ends at the landing")
    {
        // A chord slide keeps the fingers planted, so the continuity law covers the transit and the
        // span ends at the LANDING: two spans and no third. Nothing about the glide is published on
        // the span itself — the tail law never hides a ring that STATES something, so a travelling
        // member needs no span-level exemption — and what the derivation pins is where the transit
        // puts the boundaries.
        const ChartShapes sliding = deriveFrom(streamOf(chord_slide()));
        REQUIRE(sliding.shapes.size() == 2);

        // And a figure whose hand never moves is one span from end to end.
        const ChartShapes still = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
        }));
        REQUIRE(still.shapes.size() == 1);
    }

    SECTION("a slot inside the travel closes the span, and the landing it outran opens nothing")
    {
        // Shape rule 4: "a landing reached after the grip broke opens nothing". The break is a
        // CONTRADICTION — a restrike naming another stop on a string the grip states — because a
        // note sounding a string the shape does not state would be ordinary growth instead. And the
        // glide is two strings rather than three, because THE LANDING OPENS AT TWO (shape rule 4):
        // at three the surviving pair would re-open behind the break and the section would stop
        // discriminating.
        //
        // The break leaves one struck string beside one still-gliding ring: two members against a
        // three-member accumulation minimum, and an own-count of one against the statement
        // threshold of two — so nothing opens at the break either, and the landing arrives with no
        // statement standing to hand over.
        const std::vector<ChartNote> interrupted = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{1}),
                {{Fraction{1, 4}, 5}, {Fraction{1, 2}, 7}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{1}),
                {{Fraction{1, 4}, 7}, {Fraction{1, 2}, 9}}),
            noteAt(2, Fraction{}, 1, 3, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(interrupted);

        // The EXACT count, before anything indexes: "the landing opens nothing" is a claim about
        // how many spans exist, so a bound that only guarded the indexing below would let another
        // span appear unnoticed.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        everySpanIsPositive(derived);

        // THE 2-NOTE CHORD SLIDE (shape rule 4) — the control that proves the refusal above is the
        // BREAK and not the width. The same two-string glide with nothing interrupting it lands its
        // pair and hands the grip over, which a ≥3 boundary minimum would refuse outright.
        const ChartShapes uninterrupted = deriveFrom(streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{1}),
                {{Fraction{1, 4}, 5}, {Fraction{1, 2}, 7}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{1}),
                {{Fraction{1, 4}, 7}, {Fraction{1, 2}, 9}}),
        }));
        REQUIRE(uninterrupted.shapes.size() == 2);
        CHECK(uninterrupted.shapes[1].place == Fraction{1, 2});
        CHECK(uninterrupted.shapes[1].landing_opened);
        REQUIRE(uninterrupted.shapes[1].posture < uninterrupted.postures.size());
        const std::vector<std::optional<ChartStop>>& landed =
            uninterrupted.postures[uninterrupted.shapes[1].posture].stops;
        CHECK(landed[0] == std::optional{fretStop(7)});
        CHECK(landed[1] == std::optional{fretStop(9)});
        everySpanIsPositive(uninterrupted);
    }

    SECTION("a landing on another chord's onset steals no box")
    {
        // The zero-length successor: the glide lands exactly where a chord
        // on other strings is struck. A successor there would have no length at all, and drawing
        // it would put a bracket on the very instant the strike's own box states — so it is never
        // emitted, and the struck chord keeps its start and its whole extent.
        std::vector<ChartNote> landing_on_a_chord = chord_slide();
        landing_on_a_chord.push_back(noteAt(3, Fraction{}, 5, 3, Fraction{1, 2}));
        landing_on_a_chord.push_back(noteAt(3, Fraction{}, 6, 3, Fraction{1, 2}));
        const ChartShapes derived = deriveFrom(streamOf(landing_on_a_chord));

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        everySpanIsPositive(derived);

        // The discrimination, a quarter apart: a landing that lands BEFORE the chord has room of
        // its own, and re-opens there.
        std::vector<ChartNote> landing_early = chord_slide();
        landing_early.push_back(inMeasure(2, noteAt(1, Fraction{}, 5, 3, Fraction{1, 2})));
        landing_early.push_back(inMeasure(2, noteAt(1, Fraction{}, 6, 3, Fraction{1, 2})));
        const ChartShapes breathing = deriveWith(streamOf(landing_early), makeTempoMap());
        REQUIRE(breathing.shapes.size() == 3);
        CHECK(breathing.shapes[1].place == Fraction{1, 2});
        everySpanIsPositive(breathing);
    }

    SECTION("the edge (b) suppression does not flip across a signature change")
    {
        // The first meter-change figure in this suite, and it is here because the quantum is a
        // DURATION, which no signature change can rescale — and this figure is the pin that says
        // so: a 6/8 glide landing on a 4/4 downbeat is suppressed by exactly the same duration a
        // 4/4 one would be.
        //
        // The arrival sits one margin before the closing chord, which is how the chart states
        // "glides into that note". At this map's one quarter-note rate an eighth lasts two thirds
        // of a second, so the margin measures 3/320 of a whole note there. The closing chord
        // has to be a FOREIGN grip (shape rule 4): a restrike of the LANDED grip restates the
        // successor and rides inside it, so it would close nothing and this figure would stop
        // asking its question.
        const TempoMap meter_change{
            metersAt(
                {{.at = "1:1", .numerator = 6, .denominator = 8},
                 {.at = "2:1", .numerator = 4, .denominator = 4}}),
            {anchor("1:1", 0), anchor("5:1", 20000)},
        };
        const std::vector<ChartNote> glide_into_chord = streamOf({
            travellingAt(noteAt(5, Fraction{}, 1, 5, Fraction{1, 4}), {{Fraction{77, 320}, 8}}),
            travellingAt(noteAt(5, Fraction{}, 2, 7, Fraction{1, 4}), {{Fraction{77, 320}, 10}}),
            travellingAt(noteAt(5, Fraction{}, 3, 9, Fraction{1, 4}), {{Fraction{77, 320}, 11}}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 3, Fraction{1, 4})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 5, Fraction{1, 4})),
        });
        const ChartShapes derived = deriveWith(glide_into_chord, meter_change);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{1, 2});
        CHECK(derived.shapes[0].sustain == Fraction{77, 320});
        CHECK(derived.shapes[1].place == Fraction{3, 4});
        everySpanIsPositive(derived);

        // The control, one ring apart: the same glide across the same signature change, with the
        // landing left to breathe: each ring runs a half note, a quarter past the 4/4 downbeat. It
        // re-opens, and the successor fronts at the landing's own place, inside the 6/8 bar.
        const std::vector<ChartNote> breathing_glide = streamOf({
            travellingAt(noteAt(5, Fraction{}, 1, 5, Fraction{1, 2}), {{Fraction{77, 320}, 8}}),
            travellingAt(noteAt(5, Fraction{}, 2, 7, Fraction{1, 2}), {{Fraction{77, 320}, 10}}),
            travellingAt(noteAt(5, Fraction{}, 3, 9, Fraction{1, 2}), {{Fraction{77, 320}, 11}}),
        });
        const ChartShapes landed = deriveWith(breathing_glide, meter_change);
        REQUIRE(landed.shapes.size() == 2);
        CHECK(landed.shapes[1].place == Fraction{237, 320});
        CHECK(landed.shapes[1].sustain == Fraction{83, 320});
        everySpanIsPositive(landed);
    }

    SECTION("THE CROSS-TEMPO TENURE READ: the quantum comes from the CLOSING onset")
    {
        // The one figure that actually discriminates the read point. The quantum is a DURATION, so
        // as a note value it is whatever the tempo THERE makes it — an eightieth of a whole note at
        // 60 BPM, a sixteenth at 300 — and a glide whose landing straddles a tempo change has two
        // candidate answers.
        //
        // A glide landing on the downbeat of a bar five times the speed of the one before it, with
        // the landed grip holding a sixteenth: five times the quantum it landed under, exactly the
        // quantum it closes under. Read at the arrival it would EMIT; read at the closing
        // onset — the convention — it is dropped by the strict test. Two spans is the pinned
        // answer; three would mean the read point had moved. The landed string 3, still sounding a
        // sixteenth past the dyad's front, joins nothing there either: a sixteenth is exactly the
        // quantum in the fast bar, and a carried member must outlive the front by MORE than it
        // (shape rule 1).
        const TempoMap into_a_fast_bar{
            commonTime(),
            {anchor("1:1", 0), anchor("2:1", 4000), anchor("3:1", 4800), anchor("5:1", 6400)},
        };
        const std::vector<ChartNote> glide_into_the_fast_bar = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{9, 8}), {{Fraction{1}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{9, 8}), {{Fraction{1}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{9, 8}), {{Fraction{1}, 11}}),
            inMeasure(2, noteAt(1, Fraction{1, 4}, 1, 3, Fraction{1, 4})),
            inMeasure(2, noteAt(1, Fraction{1, 4}, 2, 5, Fraction{1, 4})),
        });
        const ChartShapes dropped = deriveWith(glide_into_the_fast_bar, into_a_fast_bar);
        REQUIRE(dropped.shapes.size() == 2);
        CHECK(dropped.shapes[0].place == Fraction{});
        CHECK(dropped.shapes[0].sustain == Fraction{1});
        CHECK(dropped.shapes[1].place == Fraction{17, 16});
        CHECK(std::ranges::none_of(dropped.shapes, [](const ChartShape& shape) {
            return shape.landing_opened;
        }));
        everySpanIsPositive(dropped);

        // The control, the SAME notes on a map that never leaves 120 BPM: a sixteenth strictly
        // exceeds the fortieth of a whole note the quantum measures there, so the landed grip has
        // a moment of its own and is emitted. The notes are identical, so only the closing onset's
        // tempo can be what moved. The same margin now carries the landed string 3 into the dyad
        // founded at its close (shape rule 1): it outlives that front by a sixteenth, more than
        // the quantum, so it joins, and its death an eighth past the landing splits the dyad,
        // which runs on alone as a box.
        const ChartShapes emitted = deriveWith(glide_into_the_fast_bar, makeTempoMap());
        REQUIRE(emitted.shapes.size() == 4);
        CHECK(emitted.shapes[1].landing_opened);
        CHECK(emitted.shapes[1].sustain == Fraction{1, 16});
        CHECK(emitted.shapes[2].place == Fraction{17, 16});
        CHECK(emitted.shapes[2].sustain == Fraction{1, 16});
        CHECK(emitted.shapes[2].sounds_in_parts);
        CHECK(derivedStops(emitted, 2)[2] == std::optional{fretStop(11)});
        CHECK(emitted.shapes[3].place == Fraction{9, 8});
        CHECK(emitted.shapes[3].sustain == Fraction{3, 16});
        CHECK_FALSE(emitted.shapes[3].sounds_in_parts);
        CHECK(memberCount(emitted, 3) == 2);
        everySpanIsPositive(emitted);
    }

    SECTION("no travel figure emits a span with sound in it and no length")
    {
        // THE INVARIANT at full strength, over every travel shape this suite has a figure for.
        // A departure split would open each of these with a zero-length span; the landing split
        // cannot, and the property is asked of the emit path rather than of a hand-built shape.
        everySpanIsPositive(deriveFrom(streamOf(chord_slide())));
        everySpanIsPositive(deriveFrom(streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{5, 8}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{3, 4}, 11}}),
        })));
        everySpanIsPositive(deriveFrom(streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 1, 5, Fraction{5, 4}),
                {{Fraction{1, 4}, 7}, {Fraction{1, 2}, 7}, {Fraction{3, 4}, 9}}),
            travellingAt(
                noteAt(1, Fraction{}, 2, 7, Fraction{5, 4}),
                {{Fraction{1, 4}, 9}, {Fraction{1, 2}, 9}, {Fraction{3, 4}, 11}}),
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{5, 4}),
                {{Fraction{1, 4}, 11}, {Fraction{1, 2}, 11}, {Fraction{3, 4}, 13}}),
        })));
        everySpanIsPositive(deriveFrom(streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}), {{Fraction{19, 40}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}), {{Fraction{19, 40}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1, 2}), {{Fraction{19, 40}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 4}),
            noteAt(3, Fraction{}, 3, 11, Fraction{1, 4}),
        })));
    }
}

// A carried member's fret (shape rule 1), which is shape rule 4's reading of the channel asked at a
// later slot: a carried ring joins the posture at the stop its own fret channel states THERE, so a
// finger that has slid since the strike is stated where it now is and one still sliding is stated
// nowhere.
TEST_CASE("A carried ring folds into a posture at the stop its channel states", "[core][chart]")
{
    SECTION("a travelled ring folds in at the fret it LANDED on")
    {
        // A chord slide landing on the beat and ringing long, and a chord on other strings a
        // measure-and-a-bit later. The carried strings are at 7 and 9 by then, and the frets they
        // were STRUCK at (5 and 7) are two spans behind the hand.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{1, 4}, 7}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{2}), {{Fraction{1, 4}, 9}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{2}), {{Fraction{1, 4}, 11}}),
            noteAt(4, Fraction{}, 5, 3, Fraction{1, 2}),
            noteAt(4, Fraction{}, 6, 3, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // TWO spans. The departing stack ends at its landing; the grip its travels landed in then
        // GROWS to take the later chord's two stops (growth continues) instead of handing over to a
        // span of its own; and when that chord's members fall silent the carried rings open NOTHING
        // (shape rule 1 — ring-out opens nothing). What the section is about: the carried strings
        // fold in at the frets they LANDED on.
        REQUIRE(derived.shapes.size() == 2);
        CHECK_FALSE(derived.shapes[0].landing_opened);
        CHECK(derived.shapes[1].landing_opened);
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        REQUIRE(derived.shapes[1].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& carried =
            derived.postures[derived.shapes[1].posture].stops;
        REQUIRE(carried.size() >= 6);
        CHECK(carried[0] == std::optional{fretStop(7)});
        CHECK(carried[1] == std::optional{fretStop(9)});
        CHECK(carried[2] == std::optional{fretStop(11)});
        CHECK(carried[4] == std::optional{fretStop(3)});
        CHECK(carried[5] == std::optional{fretStop(3)});
        // The onset frets get their own refusal because it is the whole finding: they are a grip
        // the hand has left, and one chart cannot state two hand positions for the same fingers at
        // one instant.
        CHECK(carried[0] != std::optional{fretStop(5)});
        CHECK(carried[1] != std::optional{fretStop(7)});
    }

    SECTION("an untravelled ring folds in at its onset fret")
    {
        // The control, one keyframe apart: a channel that never leaves its stop still states that
        // stop, so the carry is unchanged.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{2}),
            noteAt(1, Fraction{}, 3, 9, Fraction{2}),
            noteAt(4, Fraction{}, 5, 3, Fraction{1, 2}),
            noteAt(4, Fraction{}, 6, 3, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // ONE span: no travel means no landing to divide it, the chord GROWS it (growth continues),
        // and the carried rings that outlive the chord's members open NOTHING when it ends
        // (shape rule 1). The control this section exists for is the posture.
        REQUIRE(derived.shapes.size() == 1);
        CHECK_FALSE(derived.shapes[0].landing_opened);
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& carried =
            derived.postures[derived.shapes[0].posture].stops;
        REQUIRE(carried.size() >= 6);
        CHECK(carried[0] == std::optional{fretStop(5)});
        CHECK(carried[1] == std::optional{fretStop(7)});
    }

    SECTION("a ring caught MID-TRAVEL folds into no posture at all")
    {
        // ONE gliding ring — held at its stop through a restating keyframe a quarter in, then
        // travelling to rest three quarters later — crossed by the same chord at three instants.
        // The only thing that moves is how far along its channel the finger is: on the stop,
        // between stops, and on the grip it landed in.
        const auto crossed_at = [](const int measure, const int beat) {
            return streamOf({
                travellingAt(
                    noteAt(1, Fraction{}, 1, 5, Fraction{2}),
                    {{Fraction{1, 4}, 5}, {Fraction{1}, 7}}),
                inMeasure(measure, noteAt(beat, Fraction{}, 5, 3, Fraction{1, 2})),
                inMeasure(measure, noteAt(beat, Fraction{}, 6, 3, Fraction{1, 2})),
            });
        };
        // The posture of the span the crossing chord is IN — the first, since the chord's statement
        // is what the fold-in question is asked of. It is the only span in the first two figures;
        // in the third the chord is struck exactly where the carry's travel arrives, so shape rule
        // 4 tiles a landing successor onto it (the crossing itself is unchanged, which is why the
        // helper names the span rather than the count).
        const auto carried_frets =
            [](const std::vector<ChartNote>& notes) -> std::vector<std::optional<ChartStop>> {
            const ChartShapes derived = deriveFrom(notes);
            REQUIRE_FALSE(derived.shapes.empty());
            REQUIRE(derived.shapes.front().posture < derived.postures.size());
            const std::vector<std::optional<ChartStop>>& frets =
                derived.postures[derived.shapes.front().posture].stops;
            REQUIRE(frets.size() >= 6);
            return frets;
        };

        // On the departure itself the channel still states the stop, so the carry is ordinary.
        const std::vector<std::optional<ChartStop>> at_departure = carried_frets(crossed_at(1, 2));
        CHECK(at_departure[0] == std::optional{fretStop(5)});
        // Between the departure and the landing the finger is on NO stop, so it is a member of
        // nothing: the posture states the struck strings and says nothing about this one.
        const std::vector<std::optional<ChartStop>> mid_travel = carried_frets(crossed_at(1, 4));
        CHECK_FALSE(mid_travel[0].has_value());
        CHECK(mid_travel[4] == std::optional{fretStop(3)});
        CHECK(mid_travel[5] == std::optional{fretStop(3)});
        // And from the landing on it states the grip it came to rest on.
        const std::vector<std::optional<ChartStop>> at_landing = carried_frets(crossed_at(2, 1));
        CHECK(at_landing[0] == std::optional{fretStop(7)});
    }
}

// THE ONE PER-STRING RECORD: one record per sounded string, rather than two arrays over
// one fact — the STOPS each string states and the REACH each member's ring covers — which would let
// a string be in one and not the other. These pin the seam's halves: a ring the span refuses as a
// member states neither its stop nor its reach, a tap's sound chains a statement it may not bound,
// and a member's travel is seen by the span it is in.
TEST_CASE("Chart shape derivation holds one record per sounded string", "[core][chart]")
{
    SECTION("a fretted ring struck inside an emitted span states its stop and reach into the next")
    {
        // The per-string record is written for MEMBERS, and the fretted carry, struck inside a
        // span already emitted, becomes one where the chord founds (shape rule 1): a carried
        // member, so the chord's span takes its stop AND its reach. One record answers both
        // columns at once, which is what having one record per string is for.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{9, 16}),
            noteAt(1, Fraction{}, 4, 11, Fraction{1, 32}),
            noteAt(3, Fraction{}, 1, 5, Fraction{3, 4}),
            noteAt(3, Fraction{}, 2, 7, Fraction{3, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The carry founds and dates nothing, so the span dates from the chord's own stroke.
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        // The carry states its stop in the posture...
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(9)});
        // ...and its reach: the piece holding it ends at the carry's end a sixteenth past the
        // chord, not at the struck members' three quarters. The carry's death splits rather than
        // closes, so the chord runs on alone, its record dropped, to its own rings' end.
        CHECK(derived.shapes[1].sustain == Fraction{1, 16});
        CHECK(derived.shapes[2].place == Fraction{9, 16});
        CHECK(derived.shapes[2].sustain == Fraction{11, 16});
        CHECK_FALSE(derivedStops(derived, 2)[2].has_value());
        CHECK_FALSE(derived.shapes[2].sounds_in_parts);
    }

    SECTION("a tap carries the SOUND past the coverage, and the close still reads the coverage")
    {
        // What the figure pins is the TWO REACH COLUMNS the law separates, which is the same seam
        // from the other side.
        //
        // String 1's fretting-hand ring ends at beat 2 and the tap picks the string up there, so
        // `sounds` runs to beat 4 and nothing quits — the grip never breaks. But `covers`, the
        // fretting hand's own reach, is the ONLY input to the close, so the statement runs out at
        // beat 2 all the same, which is exactly what "a tap may chain a statement it may not
        // bound" means.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3, 4}),
            tapAt(2, Fraction{}, 1, 12, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& held =
            derived.postures[derived.shapes[0].posture].stops;
        CHECK(held[0] == std::optional{fretStop(5)});
        CHECK(held[1] == std::optional{fretStop(7)});
    }

    SECTION("a fold-in's own glide splits the span at the landing it makes")
    {
        // THE FOLD-IN GLIDE, and the defect two records would make unfixable: a lone ringing note
        // folds into a chord's posture and then GLIDES under it. With the travel in a record the
        // span's own reading could not see, the span would go on owning its members' tail ink
        // across a transit it did not know was happening. What this pins is the derivation alone.
        // The carry HOLDS its stop through the fold-in slot (the restating keyframe at two
        // quarters) and departs after it, so it is a member of the posture and then travels under
        // the span — which is the figure, rather than a string caught mid-glide that folds into
        // nothing.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(1, Fraction{}, 3, 9, Fraction{1}),
                {{Fraction{1, 2}, 9}, {Fraction{3, 4}, 12}}),
            noteAt(2, Fraction{}, 1, 5, Fraction{3, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The span DATES from the ringing note (the dating rule), and every sounded member bounds —
        // so the statement runs to the carry's own LANDING, where the landed grip re-opens and the
        // two tile. Two spans, and the second is the one the travel handed over.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].landing_opened);

        // The control, one keyframe apart: the same figure with the carry holding its stop states
        // no travel at all, so the statement runs end to end as one span.
        const std::vector<ChartNote> planted = streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
            noteAt(2, Fraction{}, 1, 5, Fraction{3, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
        });
        const ChartShapes still = deriveFrom(planted);
        REQUIRE(still.shapes.size() == 1);
    }
}

// SPANS NEVER OVERLAP, which two independent readers both stand on. The presentation
// rules find a note's covering span with \ref SpanCover, which keeps the FURTHEST-REACHING span
// started at or before the onset; the highway's chord grouping keeps the LATEST-STARTING one. Those
// are the same span exactly while the ends are non-decreasing — that is, while no span outlives the
// start of the one after it — so the property is pinned here rather than assumed at two sites.
//
// The derivation makes it structural: a closing event ends a span at or before its own instant, a
// contradiction divides one extent at the breaking onset, and a landing successor opens exactly
// where its predecessor closes. Every figure below is one of those shapes.
TEST_CASE("Chart shape derivation never overlaps two spans", "[core][chart]")
{
    const auto tile = [](const std::vector<ChartNote>& notes) {
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() >= 2);
        for (std::size_t index = 1; index < derived.shapes.size(); ++index)
        {
            const ChartShape& earlier = derived.shapes[index - 1];
            const ChartShape& later = derived.shapes[index];
            CHECK(!(later.place < earlier.place + earlier.sustain));
        }
    };

    SECTION("a chug run closed by a foreign chord")
    {
        tile(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 2}),
        }));
    }

    SECTION("a contradiction dividing one ring")
    {
        // A growth would not divide this ring at all — growth continues, so the shape stays one
        // span and there is nothing to tile. A CONTRADICTION does divide a ring — the hand moves to
        // another stop on a string the grip states — and that is the shape this section needs.
        tile(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 10, Fraction{1, 2}),
        }));
    }

    SECTION("a chord slide tiling into its landing successor")
    {
        tile(streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{7, 16}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{7, 16}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{7, 16}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 4}),
        }));
    }
}

// THE OPENING MARK'S ANCHOR, published by the walk (shape rule 4). One field with one write rule:
// every span an EVENT states seeds it at its own start, and the first interior SOUNDING fills the
// slot a landing left empty.
TEST_CASE("Chart shape derivation publishes each span's opening mark", "[core][chart]")
{
    SECTION("an event-opened span carries its own start")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        // The strum states the shape, so the mark belongs at the strum — not at the restrike
        // inside, which states nothing the span did not already say.
        CHECK(derived.shapes.front().bracket_place == std::optional{Fraction{}});
    }

    SECTION("a landing successor defers to its first interior sounding")
    {
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{7, 16}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{7, 16}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{7, 16}, 11}}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        // Bound ONCE and guarded by that name: two `shapes[1]` subscripts are two calls the
        // optional checker cannot prove yield the same object, and it cannot see through REQUIRE.
        const ChartShape& successor = derived.shapes[1];
        CHECK(successor.landing_opened);
        // The landing itself states nothing, so the mark waits for the re-pick.
        CHECK(successor.bracket_place == std::optional{Fraction{1, 2}});
        REQUIRE(successor.bracket_place.has_value());
        if (successor.bracket_place.has_value())
        {
            CHECK(successor.place != *successor.bracket_place);
        }
    }

    SECTION("a successor that never sounds interiorly carries no mark at all")
    {
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 8}}),
            travellingAt(noteAt(1, Fraction{}, 2, 7, Fraction{1}), {{Fraction{1, 2}, 10}}),
            travellingAt(noteAt(1, Fraction{}, 3, 9, Fraction{1}), {{Fraction{1, 2}, 11}}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].landing_opened);
        CHECK_FALSE(derived.shapes[1].bracket_place.has_value());
    }

    SECTION("a lone glide's landing opens the span, whose mark draws at its first sounding")
    {
        // A slide lands on 13 and rings on while an open string and then a fret sound around it.
        // The landing begins a statement as a strike does, so it opens a span there with one
        // survivor; the two notes grow it to three and found it, fronted at the landing. A landing
        // is no sounding, so the mark draws at the first sounding inside, the open string's.
        const ChartShapes derived = deriveFrom(streamOf({
            travellingAt(noteAt(2, Fraction{}, 3, 11, Fraction{5, 8}), {{Fraction{1, 8}, 13}}),
            noteAt(3, Fraction{}, 4, 0, Fraction{3, 8}),
            noteAt(3, Fraction{1, 2}, 5, 12, Fraction{1, 8}),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes.front().place == Fraction{3, 8});
        CHECK(derived.shapes.front().landing_opened);
        CHECK(derived.shapes.front().bracket_place == std::optional{Fraction{1, 2}});
        CHECK(memberCount(derived, 0) == 3);
        everySpanIsPositive(derived);
    }
}

// THE ACCUMULATION LAW, probed figure by figure. Each section is one of the constraint figures the
// law was walked against, and what it pins is the DERIVATION's own answer for it — the front, the
// extent, the founding and the class — because those four are what every surface then reads.
TEST_CASE("Chart shape derivation opens a span where rings accumulate", "[core][chart]")
{
    SECTION("THE BROKEN CHORD: one note at a time, bracketed from its FIRST note")
    {
        // The founding figure — the let-ring broken chord whose plucks pile up into a held shape.
        // Under shape rule 1's statement threshold alone it would derive NOTHING: every slot holds
        // one member, so no span would open and the figure would read as four bare notes. The
        // opening law reads the rings instead, and the dating rule puts the bracket's front at the
        // FIRST pluck rather than at whichever arrival reached the threshold.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 3, Fraction{1}),
            noteAt(2, Fraction{}, 2, 5, Fraction{3, 4}),
            noteAt(3, Fraction{}, 3, 5, Fraction{1, 2}),
            noteAt(4, Fraction{}, 4, 4, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        // Every ring ends together at beat five, so the statement runs the whole figure and the
        // posture is the whole grip — the four stops the hand built up.
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(memberCount(derived, 0) == 4);
        // ARPEGGIO by construction, not by a rule of its own: the opening slot strikes one string
        // where the shape sounds four.
        CHECK(derived.shapes[0].sounds_in_parts);
        REQUIRE(arpeggiosFrom(notes).size() == 1);
        CHECK(arpeggiosFrom(notes).front());
        // The rails run from the front, so the mark does too, and no digit stack stands there for
        // members whose own heads are still to arrive.
        CHECK(derived.shapes[0].bracket_place == std::optional{derived.shapes[0].place});
        everySpanIsPositive(derived);
    }

    SECTION("FOUNDING DECIDES NOTHING: a strum absorbs exactly as an accumulation does")
    {
        // GROWTH IS ACCUMULATION, so how a shape was founded decides nothing and there is no
        // founding mode to read. Both figures add a NEW stop to a standing shape, and both derive
        // ONE span.
        //
        // (a) STRUCK TOGETHER: two strings said "this grip, now"; the third stop is one the grip
        //     merely lacks, and the quit arm — not a founding mode — is what guarantees the union
        //     is honest, since a dead ring would have broken the span first.
        const std::vector<ChartNote> strum = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 3, 9, Fraction{3, 4}),
        });
        const ChartShapes stated = deriveFrom(strum);
        REQUIRE(stated.shapes.size() == 1);
        CHECK(stated.shapes[0].place == Fraction{});
        CHECK(memberCount(stated, 0) == 3);
        CHECK(stated.shapes[0].sustain == Fraction{1});

        // (b) ARRIVING APART: the same three stops one at a time. Same span, same posture, same
        //     length — which is the point.
        const std::vector<ChartNote> built_up = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(3, Fraction{}, 3, 9, Fraction{1, 2}),
        });
        const ChartShapes absorbed = deriveFrom(built_up);
        REQUIRE(absorbed.shapes.size() == 1);
        CHECK(absorbed.shapes[0].place == Fraction{});
        CHECK(memberCount(absorbed, 0) == 3);
        CHECK(absorbed.shapes[0].sustain == Fraction{1});
    }

    SECTION("A CONTRADICTION splits either founding")
    {
        // The control the absorption needs: growth is admitted, a moved FINGER never is. The
        // last onset restates string 1 at another fret, which is a stop the shape already states
        // differently — and no founding absorbs that. Three strings accumulate first, because a
        // contradiction needs a STANDING span to split and two members state none.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(2, Fraction{1, 2}, 3, 9, Fraction{5, 8}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        // The split is the CLOSE: the accumulation ends at the contradicting onset.
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 2}});
        // The contradicting strike then founds NOTHING over the two rings crossing it: they were
        // struck behind the close, and sound alone counts only statements made since the coverage
        // frontier (shape rule 1), so the lone 8 over spent rings is one statement, short of any
        // accumulation. The span exists on its own onsets or not at all.
        everySpanIsPositive(derived);
    }

    SECTION("RING-OUT OPENS NOTHING: the shape ends at its first death and hands nothing on")
    {
        // SHAPE RULE 1. TERMINATION and the posture truth criterion (shape rule 3): four members
        // ring for three, four, five and six quarters, and the statement ends at the first death
        // because no span may claim a stop the hand abandoned while it ran. There is no CHAIN — the
        // survivors open no seamless death-successor holding the smaller shape. Strings that merely
        // ring on past a break are TAILS, so nothing is stated after beat 4 and the three
        // survivors' rings simply draw.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 9, Fraction{5, 4}),
            noteAt(1, Fraction{}, 4, 11, Fraction{3, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK(memberCount(derived, 0) == 4);
        // ONE cause remains for this field, and a death is not it (the census keys landing-born
        // spans on exactly this).
        CHECK_FALSE(derived.shapes[0].landing_opened);
        everySpanIsPositive(derived);
    }

    SECTION("A REPLACEMENT is no death: a chug chain stays one statement")
    {
        // The discrimination the death law needs, and the reason the boundary test reads the
        // slot's own rings: these rings end exactly at their own restrikes, which is the
        // strike-into-strike shape a stored chug chain has. A boundary here would fracture every
        // repeated strum in the corpus.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
    }

    SECTION("A MIXED CHUG is a replacement too, where only ONE member's ring ends there")
    {
        // The discriminator: the section above cannot tell the replacement reading from
        // a boundary that simply never happens, because its rings all end together and all are
        // struck again. Here they do not — strings 2 and 3 ring straight through beat 2, where
        // string 1's ring ends and string 1 alone is restruck.
        //
        // The statement is in force at that instant (the one authority: every member is sounding,
        // string 1 by being replaced), so nothing dies and the whole grip runs on. Without the
        // replacement reading string 1 DIES there, the grip breaks at beat 2, and the posture after
        // it loses fret 5 — which is what the posture assertion catches, since both readings agree
        // about where the figure ends.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(1, Fraction{}, 3, 9, Fraction{3, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        // THE ABSORPTION RULE leaves the figure UNCUT: the founding chug does not stand alone —
        // string 1 is picked again at beat 2 while strings 2 and 3 ring on — so it is absorbed and
        // one statement carries the whole grip in parts. The replacement reading is the whole
        // discrimination: string 1 is REPLACED at beat 2, not dead, so the statement is in force
        // there and the span runs on rather than quitting. Without it fret 5 would leave the
        // posture, which is what the posture assertion catches.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK_FALSE(derived.shapes[0].landing_opened);
        CHECK(memberCount(derived, 0) == 3);
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].stops[0] == std::optional{fretStop(5)});
    }

    SECTION("RENEWAL IS PER STRING: the member nobody restruck quits, whatever else sounds")
    {
        // The discrimination the mixed chug above cannot make on its own: there the string whose
        // ring ENDS is the one restruck, so a rule that renewed a whole slot's worth of strings
        // would answer identically. Here the two are pulled apart — string 1's ring ends at beat 2
        // and the slot restrikes string 2 instead.
        //
        // A string's expiring evidence is renewed iff THIS slot sounds THAT string, so string 1
        // quits with nothing renewing it and the grip breaks at its death. Nothing opens after it:
        // the restruck string plus the one ring still crossing is two members, under the
        // three-member accumulation minimum and under the two-stop statement threshold.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(1, Fraction{}, 3, 9, Fraction{3, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(memberCount(derived, 0) == 3);
        everySpanIsPositive(derived);
    }

    SECTION("THE LET-RING BOUNDARY CHORD: nothing is renewed, so the break precedes the chord")
    {
        // The let-ring figure's rings all end on the boundary chord's own onset, and that chord
        // sounds none of their strings — so no expiring evidence is renewed anywhere, the grip
        // breaks BEFORE the slot is judged, and the chord is released to the ordinary opening law.
        // It opens fresh: two spans tiling at the boundary, and the second states the chord's stops
        // alone. The dead rings are not carried into it — a ring ending AT a slot crosses no slot
        // (the membership window is read strictly).
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1, 2}),
            noteAt(3, Fraction{}, 4, 3, Fraction{1, 2}),
            noteAt(3, Fraction{}, 5, 3, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(memberCount(derived, 1) == 2);
        CHECK_FALSE(derived.shapes[1].landing_opened);
        everySpanIsPositive(derived);

        // The other arm of the same law: a boundary that sounds only ONE string opens nothing at
        // all, because a break releases the instant's onsets to the opening law and no exemption
        // waits anywhere for the rings that just died.
        const ChartShapes lone = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1, 2}),
            noteAt(3, Fraction{}, 4, 3, Fraction{1, 2}),
        }));
        REQUIRE(lone.shapes.size() == 1);
        CHECK(lone.shapes[0].sustain == Fraction{1, 2});
    }

    SECTION("GROWTH HAS NO DISJOINT-GRIP GUARD: a chord over a sounding grip UNIONS with it")
    {
        // A chord on strings the standing grip does not touch AT ALL still grows it — there is no
        // disjoint-grip guard anywhere in the break verdict, and none is needed.
        //
        // THE QUIT-ARM-FIRST PROOF, which is why sound-overlap is the only gate the union needs: a
        // member whose sound is out with nothing renewing it BREAKS the grip before any statement
        // at that instant is judged. So absorption can only ever union grips whose sounds genuinely
        // overlap — a dead ring fires the quit arm first, and there is nothing left to absorb into.
        // The control below is that proof run as a figure.
        const std::vector<ChartNote> overlapping = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{}, 5, 3, Fraction{1, 2}),
            noteAt(2, Fraction{}, 6, 3, Fraction{1, 2}),
        });
        const ChartShapes unioned = deriveFrom(overlapping);
        REQUIRE(unioned.shapes.size() == 1);
        CHECK(unioned.shapes[0].place == Fraction{});
        CHECK(memberCount(unioned, 0) == 4);
        everySpanIsPositive(unioned);

        // The control, one ring apart: the first grip's rings END where the chord lands, so the
        // quit arm fires at that instant and the chord opens fresh. Two spans, two disjoint
        // postures — the union above happened because the sounds overlapped, and for no other
        // reason.
        const ChartShapes broken = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(2, Fraction{}, 5, 3, Fraction{1, 2}),
            noteAt(2, Fraction{}, 6, 3, Fraction{1, 2}),
        }));
        REQUIRE(broken.shapes.size() == 2);
        CHECK(broken.shapes[0].sustain == Fraction{1, 4});
        CHECK(memberCount(broken, 0) == 2);
        CHECK(broken.shapes[1].place == Fraction{1, 4});
        CHECK(memberCount(broken, 1) == 2);
        everySpanIsPositive(broken);
    }

    SECTION("THE DATING RULE: a carry crossing covered ground dates nothing")
    {
        // The 182-class figure, and the defect the dating rule prevents. String 3 is struck
        // inside a span of its own and rings on across the later chord; dating the chord's span
        // from that ring's onset would put it INSIDE a span already emitted, which is two
        // statements claiming one instant. A ring struck behind the coverage frontier dates
        // nothing (shape rule 1): the chord's span dates from its own stroke, and the carry only
        // joins it there.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
            noteAt(1, Fraction{}, 4, 11, Fraction{1, 32}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 32});
        // No overlap, which is the whole promise: the second span starts at its own stroke.
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        // The fretted carry still sounding there is a finger down, so it joins the grip the chord
        // founds as a carried member (shape rule 1), making the figure sound in parts. It dies
        // with the chord's own rings, so the span closes whole rather than splitting.
        CHECK(memberCount(derived, 1) == 3);
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(9)});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].sounds_in_parts);
    }

    SECTION("A restrike at the coverage frontier dates the next span from the close")
    {
        // The My Sacrifice bar-11 figure. String 2's fret 2 fronts the first span and rings
        // exactly to its close, where the same fret is struck again and rings on under the next
        // accumulation. Every ring of the first span ends at the close, so nothing else of the
        // figure sounds at the restrike: it is the next figure's first articulation and begins its
        // own statement there (re-articulation). Had a member still sounded, the beginning it
        // inherited would lie inside the emitted span and the dating floor would clamp it to the
        // close all the same — either way the restrike dates the span from the close, never from
        // the next member's onset an eighth late, and the two spans tile.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 2, 2, Fraction{3, 8}),
            noteAt(1, Fraction{1, 2}, 4, 0, Fraction{1, 4}),
            noteAt(2, Fraction{}, 3, 4, Fraction{1, 8}),
            noteAt(2, Fraction{1, 2}, 2, 2, Fraction{11, 8}),
            noteAt(3, Fraction{}, 6, 0, Fraction{15, 8}),
            noteAt(3, Fraction{1, 2}, 5, 0, Fraction{2}),
            noteAt(4, Fraction{}, 4, 4, Fraction{1}),
            noteAt(4, Fraction{1, 2}, 3, 4, Fraction{7, 8}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{3, 8});
        CHECK(derived.shapes[1].place == Fraction{3, 8});
        everySpanIsPositive(derived);
    }

    SECTION("A DYAD accumulation states nothing: THREE members is the minimum")
    {
        // THE THREE-MEMBER MINIMUM, pinned head-on. A double-stop arpeggiated over its own ring is
        // two members accumulating, which reads as noise beside the figures three members find, so
        // sound alone may not found it; an AUTHORED two-note span is the escape and is owed by
        // docs/plans/todo/span-marker-redesign.md.
        //
        // Statement founding is untouched by this, which is why the arm is scoped to accumulation:
        // a struck dyad is still a chord box, pinned in the two-members-of-any-kind case above.
        const std::vector<ChartNote> dyad = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
        });
        CHECK(deriveFrom(dyad).shapes.empty());

        // The discrimination, one member apart: the same figure with a third stop accumulating
        // states a shape.
        const std::vector<ChartNote> triad = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3, 4}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(3, Fraction{}, 3, 9, Fraction{1, 4}),
        });
        const ChartShapes wider = deriveFrom(triad);
        REQUIRE(wider.shapes.size() == 1);
        CHECK(memberCount(wider, 0) == 3);
        CHECK(arpeggiosFrom(triad).front());
    }

    SECTION("BROAD FOUNDING: an open string founds exactly as a fretted one does")
    {
        // Why an open member founds, against a fretted-only boundary: the bracket's claims are PER
        // MEMBER, and an open member's 0 asserts no finger at all — only the ring, which the chart
        // already stores. So no member's claim can be false and the open string founds.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 0, Fraction{3, 4}),
            noteAt(2, Fraction{}, 2, 0, Fraction{1, 2}),
            noteAt(3, Fraction{}, 3, 0, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        // All three members are open strings, so the minimum is reached by open members alone —
        // nothing fretted is doing the founding here.
        CHECK(memberCount(derived, 0) == 3);
    }

    SECTION("THE DELIBERATE-EXTENSION DEFAULT: the rings' own evidence is ONE span")
    {
        // The default for the figure: a player extending a held shape one string at a time states
        // nothing that would divide it, so the derivation reads ONE statement — "the rings' own
        // evidence" — rather than guessing at a phrase boundary.
        // A SPLIT-VERB OVERRIDE is recorded for later design; nothing here anticipates it, which
        // is what makes this the default rather than a decision.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3, 2}),
            noteAt(2, Fraction{}, 2, 5, Fraction{5, 4}),
            noteAt(3, Fraction{}, 3, 6, Fraction{1}),
            noteAt(4, Fraction{}, 4, 7, Fraction{3, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{3, 2});
        CHECK(memberCount(derived, 0) == 4);
    }

    SECTION("DRONE UNDER MELODY: the drones found with the first melody note and nothing after")
    {
        // One of the six constraint figures the tail-cap rule was verified against. Long open
        // drones under a single-string melodic line: every melody note overlaps them, but a pedal
        // under a melody brackets nothing once the figure that struck it has closed.
        //
        // TWO drones, entering apart: the pair alone is under the minimum and states nothing, so
        // the first melody note is still what founds the figure.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 6, 0, Fraction{2}),
            noteAt(1, Fraction{1, 2}, 5, 0, Fraction{15, 8}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 7, Fraction{1, 4}),
            noteAt(4, Fraction{}, 1, 8, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The drones found with the first melody note, and the span dates from the earlier of
        // their onsets. The second melody note states a DIFFERENT stop on the same string — a
        // contradiction, which breaks the grip — and founds NOTHING over the drones still
        // sounding: they were struck behind the close, so the lone fresh note is no accumulation
        // (shape rule 1). The drones ring on as plain tails under the rest of the line.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 2}});
        CHECK(memberCount(derived, 0) == 3);
        CHECK(derivedStops(derived, 0)[4] == std::optional{fretStop(0)});
        CHECK(derivedStops(derived, 0)[5] == std::optional{fretStop(0)});
        everySpanIsPositive(derived);
    }

    SECTION("DRONE UNDER STABS: a carried drone folds in, as the accumulation law derives it")
    {
        // The other habitat the law covers: a ringing drone crossed by struck chords. A fresh ring
        // folds in like any other member (shape rule 1), and a member carried in rather than
        // struck makes the span an arpeggio (shape rule 5), so for the FIRST stab the drone joins
        // the posture and the span reads ARPEGGIO — and what the accumulation law adds is the
        // FRONT: the statement runs from the drone's own onset, because that is where the figure
        // began and nothing precedes it.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 6, 0, Fraction{3, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(2, Fraction{}, 2, 5, Fraction{1, 8}),
            noteAt(4, Fraction{}, 1, 5, Fraction{1, 8}),
            noteAt(4, Fraction{}, 2, 5, Fraction{1, 8}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        // FOUNDING FOLLOWS COMPOSITION: the stab struck two of the three stops the
        // span holds, so the slot did not state the shape WHOLE — the drone arrived a quarter
        // earlier and the span dates from THERE. A simultaneous strike founds a STATEMENT only
        // where its own members are the whole shape, which is what makes the front and the founding
        // one story rather than two.
        CHECK(memberCount(derived, 0) == 3);
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(arpeggiosFrom(notes).front());
        // The SECOND stab: the drone is ringing on out of the span the first stab's rings closed.
        // The stab founds on its own two strokes and dates from them; the drone still sounding
        // there joins the grip as a carried member (shape rule 1), one the stab did not strike, so
        // this span too sounds in parts. It ends where the stab's own rings do, the drone ringing
        // on past it as a tail.
        CHECK(derived.shapes[0].sustain == Fraction{3, 8});
        CHECK(derived.shapes[1].place == Fraction{3, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        CHECK(memberCount(derived, 1) == 3);
        CHECK(derivedStops(derived, 1)[5] == std::optional{fretStop(0)});
        CHECK(derived.shapes[1].sounds_in_parts);
        everySpanIsPositive(derived);
    }

    SECTION("THE MONSTER FIGURE IS BOUNDED: a long texture ends at its first member death")
    {
        // A 128-member relay, in miniature. Without the death law a bracket could run for as long
        // as ANY pair overlapped, and the corpus holds conjunctions spanning twenty measures. The
        // death law makes that impossible: each statement ends at the FIRST of its members to fall
        // silent, so however long the texture runs, every fret a bracket prints was held for every
        // instant it covers.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{1, 2}, 2, 7, Fraction{2}),
            noteAt(2, Fraction{}, 3, 9, Fraction{2}),
            inMeasure(2, noteAt(1, Fraction{}, 4, 11, Fraction{2})),
        });
        const ChartShapes derived = deriveFrom(notes);

        // The first member dies at beat three, so the span it founded stops there rather than
        // running to the far end of a texture it stopped being part of. The relay states nothing
        // further: the strike a measure later is one fresh note over two long rings struck behind
        // the close, which founds nothing (shape rule 1), so the long rings ring out as tails.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(memberCount(derived, 0) == 3);
        everySpanIsPositive(derived);
    }
}

// THE WALK-LEVEL SOUNDING TABLE (LAW A). A span records only its OWN members' stops, and a ring
// can outlive the span that covered it: a claim moves a finger off the stop a string was sounding,
// and that string goes on sounding with no span recording it. To the span that follows the string
// would be silent ground — so a strike stating ANOTHER stop on it would read as ordinary growth
// and be absorbed, printing a posture over a finger that had demonstrably moved. The hand table is
// the witness, and its contradiction window is END-INCLUSIVE because the same-string clamp puts
// the old ring's end exactly ON the contradicting strike: that junction instant is the only one at
// which the two coexist, so an exclusive read can never see it.
//
// THE UNISON RESTATEMENT SPLIT, sighted on the corpus's picked-verse vamp: a stroke striking EVERY
// stop a sounds-in-parts span states is the whole grip said again in unison — a chord statement, no
// longer the arpeggio's texture — so the texture closes there and the stroke founds a chord span
// through the ordinary slot open. The guards are the rule's own text and each has a section or a
// standing fixture: a PARTIAL restatement rides as texture (second section), a plain chord's
// restrike chain — never in parts — continues (the derived chug-chain fixture in the presentation
// suite), and the FOUNDING strum never splits because no span stands at its own open (the
// founding-strum fixture beside it).
TEST_CASE("A unison restatement of the whole grip founds a chord span", "[core][chart]")
{
    // Three plucks accumulate into a texture whose rings all reach the next downbeat.
    const auto texture = [](const Fraction third_ring) {
        return std::vector<ChartNote>{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(3, Fraction{}, 3, 9, third_ring),
        };
    };

    SECTION("the whole grip struck in unison closes the texture and opens a chord")
    {
        std::vector<ChartNote> notes = texture(Fraction{1, 2});
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 1, 5, Fraction{1, 4})));
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 2, 7, Fraction{1, 4})));
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 3, 9, Fraction{1, 4})));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));

        // The texture runs from its earliest member to the strum, and the strum's chord span
        // stands after it — the bracket ends where the arpeggio ends, which is the rule's whole
        // point: it is no longer an arpeggio.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(derived.shapes[1].place == Fraction{1});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
        everySpanIsPositive(derived);
    }

    SECTION("a strum the resuming texture absorbs flows inside the bracket, box and all")
    {
        // THE ABSORPTION RULE: the 2:1 strum does not stand alone — the very next slot picks one of
        // its own stops while two of its members still ring — so it is ABSORBED and the texture
        // flows straight through it. Only STRUCTURE is absorbed; the box survives, AS A BOX, drawn
        // on the strum inside the bracket, and is pinned in its own case above. Rings tile seam to
        // seam throughout so the quit arm never speaks and the splits are the only law asked.
        std::vector<ChartNote> notes = texture(Fraction{1, 2});
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 1, 5, Fraction{1, 4})));
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 2, 7, Fraction{1, 2})));
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 3, 9, Fraction{3, 4})));
        notes.push_back(inMeasure(2, noteAt(2, Fraction{}, 1, 5, Fraction{3, 4})));
        notes.push_back(inMeasure(2, noteAt(3, Fraction{}, 2, 7, Fraction{1, 2})));
        notes.push_back(inMeasure(2, noteAt(4, Fraction{}, 3, 9, Fraction{1, 4})));
        notes.push_back(inMeasure(3, noteAt(1, Fraction{}, 1, 5, Fraction{1, 4})));
        notes.push_back(inMeasure(3, noteAt(1, Fraction{}, 2, 7, Fraction{1, 4})));
        notes.push_back(inMeasure(3, noteAt(1, Fraction{}, 3, 9, Fraction{1, 4})));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));

        // One bracket over the whole running figure, and the closing strum — which nothing
        // follows, so it DOES stand alone — founding its own chord span.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{2});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(derived.shapes[1].place == Fraction{2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
        everySpanIsPositive(derived);
    }

    SECTION("a superset strum is a new chord statement, never growth")
    {
        // THE WIDENING, sighted on the corpus: a stroke striking the whole grip AND a string the
        // span never stated is a new chord statement even where the span never sounded in parts,
        // so it splits rather than growing the span in place — a rung dyad followed by the full
        // chord strummed is two statements, not a dyad quietly growing into a figure that later
        // texture then brackets whole. The absorption rule leaves the SUPERSET half alone: the slot
        // after that strum is another unison, not a proper subset, so the judgment commits and the
        // boundary stands.
        //
        // The chug that follows continues the chord span, and it does not stand alone — the partial
        // restrike at its tail picks one of its own stops while two of its members ring on — so the
        // chug is ABSORBED and the span turns to parts THERE, in place. Splitting at the partial
        // instead would give the tail a third span, which is the fragmentation this rule prevents.
        // Both strums keep their boxes inside the one bracket.
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 3, 9, Fraction{1, 4}),
            noteAt(4, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(4, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(4, Fraction{}, 3, 9, Fraction{1, 4}),
        };
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 3, 9, Fraction{1, 4})));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        // The superset strum's own span, running through the absorbed chug and over the partial
        // that absorbed it, to the last renewed ring.
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{3, 4});
        CHECK(derived.shapes[1].sounds_in_parts);
        everySpanIsPositive(derived);
    }

    SECTION("a partial restatement rides as the texture's own")
    {
        // The third member's ring runs PAST the restrike instant, so nothing quits there and
        // the guard is the only thing being asked: two of three strings restruck is texture,
        // not a chord statement, and the span continues to the renewed rings' end.
        std::vector<ChartNote> notes = texture(Fraction{3, 4});
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 1, 5, Fraction{1, 4})));
        notes.push_back(inMeasure(2, noteAt(1, Fraction{}, 2, 7, Fraction{1, 4})));
        const ChartShapes derived = deriveFrom(streamOf(std::move(notes)));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{5, 4});
        everySpanIsPositive(derived);
    }
}

// THE ABSORPTION RULE, which DECOUPLES two laws a single verdict would conflate.
//
// THE BOX LAW is display and UNCONDITIONAL: simultaneously struck notes wear a chord box, no
// exception, INSIDE spans included — the drone-under-stabs figure's first stab, boxed inside the
// bracket its drone founded, is the standing precedent. Nothing in the derivation gates it, and
// this case proves the claim through the real display path rather than asserting it: the grouping
// boxes a co-struck group on its own fretting-hand count and never reads the covering span's class.
//
// THE SPAN LAW is structure and CONDITIONAL: a whole-grip stroke — the unison restatement — is a
// span BOUNDARY only where it STANDS ALONE. Where same-hold material sounds IN PARTS inside the
// stroke's own ring extent, the stroke is ABSORBED: the standing span flows through it, its
// members fold in as same-stop restatements, and it wears its box inside the span. The evidence is
// never the ring-divergence of the stroke's OWN members; it is what FOLLOWS within the rings,
// which is why the whole settlement is one predicate over the stroke and the slot after it.
TEST_CASE("An absorbed unison stroke keeps the span flowing", "[core][chart]")
{
    SECTION("THE SIGHTED FIGURE: parts sounding under the stroke's rings absorb it")
    {
        // The acceptance figure, in its timing shape: a parts span holding four stops, a stroke
        // striking ALL FOUR with one long ring and three short ones, and an eighth later the
        // same hold sounding in parts on three of them while the long one is still ringing. Cutting
        // this into THREE spans — the texture up to the stroke, a one-slot box AT the stroke, and
        // the resumed parts after it — is exactly the fragmentation absorption prevents. The
        // stroke is absorbed and one bracket flows.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(3, Fraction{}, 4, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 5, 8, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 2, 0, Fraction{1, 8}),
            noteAt(3, Fraction{1, 2}, 3, 0, Fraction{1, 8}),
            // The stroke: the whole grip in unison, the second string ringing a whole quarter past
            // the parts that follow it.
            noteAt(4, Fraction{}, 2, 0, Fraction{1, 4}),
            noteAt(4, Fraction{}, 3, 0, Fraction{1, 8}),
            noteAt(4, Fraction{}, 4, 7, Fraction{1, 8}),
            noteAt(4, Fraction{}, 5, 8, Fraction{1, 8}),
            // Same hold, in parts: a proper subset at the very same stops.
            noteAt(4, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
            noteAt(4, Fraction{1, 2}, 4, 7, Fraction{1, 4}),
            noteAt(4, Fraction{1, 2}, 5, 8, Fraction{1, 4}),
            inMeasure(2, noteAt(1, Fraction{}, 2, 0, Fraction{5, 8})),
            inMeasure(2, noteAt(1, Fraction{1, 2}, 3, 0, Fraction{1, 2})),
            inMeasure(2, noteAt(1, Fraction{1, 2}, 4, 7, Fraction{1, 2})),
            inMeasure(2, noteAt(1, Fraction{1, 2}, 5, 8, Fraction{1, 2})),
        });
        const ChartShapes derived = deriveFrom(notes);

        // ONE span, flowing from the texture's own front through the stroke to the rings' end.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 2});
        CHECK(derived.shapes[0].sustain == Fraction{9, 8});
        CHECK(derived.shapes[0].sounds_in_parts);
        REQUIRE(arpeggiosFrom(notes).size() == 1);
        CHECK(arpeggiosFrom(notes).front());
        everySpanIsPositive(derived);

        // THE BOX LAW, through the display's own path: the absorbed stroke is still four notes
        // struck at one instant, so it wears its full chord box INSIDE the bracket. Nothing about
        // the box asks what class the span it lies in has.
        const HighwayChordGrouping boxes = boxesFrom(notes);
        const std::size_t stroke_group = boxes.note_group[indexAt(notes, 1, 4, 2)];
        REQUIRE(stroke_group < boxes.groups.size());
        CHECK(boxes.groups[stroke_group].fretting_hand_count == 4);
        CHECK(boxes.groups[stroke_group].box_treatment == HighwayChordBoxTreatment::Full);
    }

    SECTION("a STANDALONE uneven chord keeps its boundary and rings past its own box")
    {
        // The stroke stands alone: nothing follows it at all, so no same-hold parts can sound
        // inside its rings and the chord judgment commits. Its members ring
        // unevenly — the box ends at the shortest, and the long member's drawn tail runs past it,
        // which is the picture the rule deliberately leaves standing.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(3, Fraction{}, 3, 9, Fraction{1, 2}),
            inMeasure(2, noteAt(1, Fraction{}, 1, 5, Fraction{3, 4})),
            inMeasure(2, noteAt(1, Fraction{}, 2, 7, Fraction{1, 8})),
            inMeasure(2, noteAt(1, Fraction{}, 3, 9, Fraction{1, 8})),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(derived.shapes[1].place == Fraction{1});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
        everySpanIsPositive(derived);

        // The long tail draws VISIBLY past the box: the span closes at the shortest member while
        // the drawn ring of the long one runs on.
        const TempoMap tempo_map = makeTempoMap();
        const ChartConnections connections = chartConnections(notes, tempo_map);
        const std::vector<Fraction> ink_end =
            chartPresentation(connections, HeadMargins{connections.places, tempo_map}, tempo_map)
                .ink_end;
        CHECK(derived.shapes[1].sustain < ink_end[indexAt(notes, 2, 1, 1)]);
    }

    SECTION("a CHUG CHAIN is untouched: every next event is another unison")
    {
        // Each stroke's next slot restates the SAME set, which is not a proper subset, so nothing
        // is absorbed and the chain derives one never-in-parts span. The repeat identity therefore
        // reads the same inputs absorption never touches, which is the whole of why rules 8 and 11
        // are protected by nature — pinned here through the display path.
        const auto chug = [](const int beat, const int string, const int fret) {
            ChartNote note = noteAt(beat, Fraction{}, string, fret, Fraction{1, 4});
            note.dead = true;
            return note;
        };
        const std::vector<ChartNote> notes = streamOf({
            chug(1, 1, 5),
            chug(1, 2, 7),
            chug(2, 1, 5),
            chug(2, 2, 7),
            chug(3, 1, 5),
            chug(3, 2, 7),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        everySpanIsPositive(derived);

        const HighwayChordGrouping boxes = boxesFrom(notes);
        REQUIRE(boxes.groups.size() == 3);
        CHECK(boxes.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(boxes.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
        CHECK(boxes.groups[2].box_treatment == HighwayChordBoxTreatment::Repeat);
    }

    SECTION("THE CONTROL: rings that end before the parts commit the boundary")
    {
        // The absorbed figure with ONE datum changed — the stroke's long member rings only to
        // the parts slot instead of past it — so no member of the stroke is still sounding when
        // the parts arrive. They follow the stroke rather than sounding under it, the judgment
        // commits, and the three spans stand. This is the discriminating control: everything else
        // about the figure, the proper subset included, is identical to the absorbed case.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(3, Fraction{}, 4, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 5, 8, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 2, 0, Fraction{1, 8}),
            noteAt(3, Fraction{1, 2}, 3, 0, Fraction{1, 8}),
            noteAt(4, Fraction{}, 2, 0, Fraction{1, 8}),
            noteAt(4, Fraction{}, 3, 0, Fraction{1, 8}),
            noteAt(4, Fraction{}, 4, 7, Fraction{1, 8}),
            noteAt(4, Fraction{}, 5, 8, Fraction{1, 8}),
            noteAt(4, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
            noteAt(4, Fraction{1, 2}, 4, 7, Fraction{1, 4}),
            noteAt(4, Fraction{1, 2}, 5, 8, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].place == Fraction{1, 2});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[0].sounds_in_parts);
        // The stroke's own box span, one slot wide: it stood alone, so it states a chord.
        CHECK(derived.shapes[1].place == Fraction{3, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
        CHECK(derived.shapes[2].place == Fraction{7, 8});
        CHECK(derived.shapes[2].sustain == Fraction{1, 4});
        everySpanIsPositive(derived);
    }

    SECTION("THE CONTROL: a contradicting next slot commits the boundary and seams")
    {
        // The same figure again, with the parts slot moving one finger: the third string sounds a
        // DIFFERENT stop. That is not same-hold material, so nothing is absorbed — and the
        // contradiction then breaks the stroke's own span at its own instant, the two spans
        // abutting exactly at the seam.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(3, Fraction{}, 4, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 5, 8, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 2, 0, Fraction{1, 8}),
            noteAt(3, Fraction{1, 2}, 3, 0, Fraction{1, 8}),
            noteAt(4, Fraction{}, 2, 0, Fraction{1, 4}),
            noteAt(4, Fraction{}, 3, 0, Fraction{1, 8}),
            noteAt(4, Fraction{}, 4, 7, Fraction{1, 8}),
            noteAt(4, Fraction{}, 5, 8, Fraction{1, 8}),
            noteAt(4, Fraction{1, 2}, 3, 2, Fraction{1, 4}),
            noteAt(4, Fraction{1, 2}, 4, 7, Fraction{1, 4}),
            noteAt(4, Fraction{1, 2}, 5, 8, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 4);
        CHECK(derived.shapes[0].place == Fraction{1, 2});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(derived.shapes[1].place == Fraction{3, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
        // The seam: the contradicting slot closes one span and opens the next at the same instant.
        // The stroke's open string 2 still rings there, so it joins the new chord as a carried
        // member (shape rule 1) and the chord sounds in parts; its death an eighth on splits the
        // chord, which runs on alone, a box, since its own stroke was whole.
        CHECK(derived.shapes[2].place == Fraction{7, 8});
        CHECK(derived.shapes[2].sustain == Fraction{1, 8});
        CHECK(derived.shapes[2].sounds_in_parts);
        CHECK(memberCount(derived, 2) == 4);
        CHECK(derivedStops(derived, 2)[1] == std::optional{fretStop(0)});
        CHECK(derived.shapes[3].place == Fraction{1});
        CHECK(derived.shapes[3].sustain == Fraction{1, 8});
        CHECK_FALSE(derived.shapes[3].sounds_in_parts);
        CHECK(memberCount(derived, 3) == 3);
        everySpanIsPositive(derived);
    }
}

// A TAP OVER A HELD CHORD STOP KEEPS THE STATEMENT ALIVE: the tap's pitch is the stopped length,
// so the finger provably stays through the tap's ring, and the pull-off that follows restates the
// same stop — the chord is one arpeggio from its strum through the whole tapped figure, never a
// box that runs out where the first member's own ring ends under the tap. The corpus figure is
// Periphery, "It's Only Smiles" measure 82, where the wrong reading starts the span at the first
// pull-off landing instead of at the chord.
TEST_CASE("A tap over a held chord stop carries the span through it", "[core][chart]")
{
    // The chord: 7 7 11 9 across strings 1–4, the low string's ring ending exactly where the tap
    // strikes it. Then tap-and-pull-off on string 4 over the held 9, and on string 3 over the 11.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 7, Fraction{2}),
        noteAt(1, Fraction{}, 2, 7, Fraction{7, 8}),
        noteAt(1, Fraction{}, 3, 11, Fraction{1, 2}),
        noteAt(1, Fraction{}, 4, 9, Fraction{1, 4}),
        tapAt(2, Fraction{}, 4, 16, Fraction{1, 8}),
        pullOffAt(2, Fraction{1, 2}, 4, 9, Fraction{3, 4}),
        tapAt(3, Fraction{}, 3, 16, Fraction{1, 4}),
        pullOffAt(4, Fraction{}, 3, 11, Fraction{5, 8}),
    });
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sounds_in_parts);
    const std::vector<bool> arpeggio = arpeggiosFrom(notes);
    REQUIRE(arpeggio.size() == 1);
    CHECK(arpeggio[0]);
    everySpanIsPositive(derived);
}

// THE TAP'S FLOOR: the picking hand sounds a string only ABOVE the finger
// holding it, so a tap whose path reaches the standing grip's stop or below it says that finger is
// gone — the grip moved, and the span splits at the tap. Above the stop the tap rides, as the case
// before this one pins.
TEST_CASE("A tap at or below the standing grip's stop splits the span", "[core][chart]")
{
    // The chord holds 9 on string 3 for a whole note, and one onset lands on that string at beat 2.
    const auto chord_with = [](const ChartNote& onset) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
            onset,
        });
    };
    const auto span_runs = [](const std::vector<ChartNote>& notes, const Fraction extent) {
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == extent);
    };

    SECTION("a tap above the stop rides it")
    {
        span_runs(chord_with(tapAt(2, Fraction{}, 3, 12, Fraction{1, 4})), Fraction{1});
    }

    SECTION("a tap on the stop itself splits")
    {
        span_runs(chord_with(tapAt(2, Fraction{}, 3, 9, Fraction{1, 4})), Fraction{1, 4});
    }

    SECTION("a tap below the stop splits")
    {
        span_runs(chord_with(tapAt(2, Fraction{}, 3, 8, Fraction{1, 4})), Fraction{1, 4});
    }

    SECTION("a tapping slide that travels down through the stop splits at its onset")
    {
        const ChartNote slid =
            travellingAt(tapAt(2, Fraction{}, 3, 16, Fraction{1, 4}), {{Fraction{1, 8}, 8}});
        span_runs(chord_with(slid), Fraction{1, 4});
    }

    SECTION("a scrape that travels down through the stop splits at its onset")
    {
        ChartNote scrape =
            travellingAt(noteAt(2, Fraction{}, 3, 12, Fraction{1, 4}), {{Fraction{1, 4}, 3}});
        scrape.attack = NoteAttack::PickSlide;
        span_runs(chord_with(scrape), Fraction{1, 4});
    }

    SECTION("a tap over an open string the grip leaves rides, since no finger is down")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1}),
            noteAt(1, Fraction{}, 3, 0, Fraction{1}),
            tapAt(2, Fraction{}, 3, 3, Fraction{1, 4}),
        });
        span_runs(notes, Fraction{1});
    }

    SECTION("the tap that split the span holds nothing: its default is the open string")
    {
        const std::vector<ChartNote> notes = chord_with(tapAt(2, Fraction{}, 3, 9, Fraction{1, 4}));
        const ChartResolutions resolutions = chartResolutions(notes, {}, 0, makeTempoMap());
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 1, 2, 3)].has_value());
    }
}

// THE PARTIAL-SLIDE SPLIT, sighted on the corpus's verse vamp as "that chord is split mid sustain":
// a slot's statement is divided by its own notated rings when a held member's ring ends while a
// co-struck glide is IN FLIGHT — strictly after it departs, strictly before it arrives — so the
// figure necessarily sounds in parts from this very slot. Against a never-in-parts
// span it is the statement coming apart AT that slot, even where it restates the whole grip: the
// box closes there and the slot founds the parts figure, dated at its own onset and born in parts.
// A voicing-shift slide whose held strings ring the whole transit stays one statement, and a
// whole-grip travel is the chord slide (shape rule 4).
TEST_CASE("A partial slide closes the box and founds the parts figure", "[core][chart]")
{
    // Two whole chugs, then the chord restated with its fretted strings gliding away while the
    // third holds and is picked beneath the transit; the travellers land on a restrike, which is
    // the glide-into-restrike shape the landing machinery already tiles — a charter-placed
    // arrival one margin inside the ring (a fret stated exactly where the ring ends would be
    // the shift slide's arrival on the head, which opens no landing at all).
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(1, Fraction{}, 3, 9, Fraction{1, 4}),
        noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(2, Fraction{}, 3, 9, Fraction{1, 4}),
        travellingAt(noteAt(3, Fraction{}, 1, 5, Fraction{3, 8}), {{Fraction{5, 16}, 10}}),
        travellingAt(noteAt(3, Fraction{}, 2, 7, Fraction{3, 8}), {{Fraction{5, 16}, 12}}),
        noteAt(3, Fraction{}, 3, 9, Fraction{1, 8}),
        noteAt(3, Fraction{1, 2}, 3, 9, Fraction{1, 8}),
        noteAt(4, Fraction{}, 3, 9, Fraction{1, 8}),
        noteAt(4, Fraction{1, 2}, 1, 10, Fraction{1, 8}),
        noteAt(4, Fraction{1, 2}, 2, 12, Fraction{1, 8}),
        noteAt(4, Fraction{1, 2}, 3, 9, Fraction{1, 8}),
    });
    const ChartShapes derived = deriveFrom(notes);

    REQUIRE(derived.shapes.size() == 3);
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sustain == Fraction{1, 2});
    CHECK_FALSE(derived.shapes[0].sounds_in_parts);
    CHECK(derived.shapes[1].place == Fraction{1, 2});
    // The parts figure runs to the travellers' ARRIVAL, where the new grip is established (shape
    // rule 4) — a margin before the restrike, which then restates the whole grip and founds
    // nothing.
    CHECK(derived.shapes[1].sustain == Fraction{5, 16});
    CHECK(derived.shapes[1].sounds_in_parts);
    CHECK(derived.shapes[2].place == Fraction{13, 16});
    CHECK_FALSE(derived.shapes[2].sounds_in_parts);

    const std::vector<bool> arpeggio = arpeggiosFrom(notes);
    REQUIRE(arpeggio.size() == 3);
    CHECK_FALSE(arpeggio[0]);
    CHECK(arpeggio[1]);
    CHECK_FALSE(arpeggio[2]);
    everySpanIsPositive(derived);

    // The discriminating negative — the corpus's own voicing-shift chug: the fretted pair
    // slides up one fret while the held strings ring the WHOLE transit, so nothing about the
    // statement comes apart and the chug that slides is still a chug. Rings tile to the landing
    // restrike exactly as the figure above, and only the holders' ring length differs.
    const std::vector<ChartNote> shifted = streamOf({
        noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
        noteAt(1, Fraction{}, 3, 9, Fraction{1, 4}),
        travellingAt(noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}), {{Fraction{3, 16}, 6}}),
        travellingAt(noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}), {{Fraction{3, 16}, 8}}),
        noteAt(2, Fraction{}, 3, 9, Fraction{1, 4}),
        noteAt(3, Fraction{}, 1, 6, Fraction{1, 4}),
        noteAt(3, Fraction{}, 2, 8, Fraction{1, 4}),
        noteAt(3, Fraction{}, 3, 9, Fraction{1, 4}),
    });
    const ChartShapes whole = deriveFrom(shifted);
    REQUIRE(!whole.shapes.empty());
    CHECK(whole.shapes[0].place == Fraction{});
    CHECK_FALSE(whole.shapes[0].sounds_in_parts);
    CHECK_FALSE(arpeggiosFrom(shifted)[0]);
    everySpanIsPositive(whole);

    // The second discriminating negative — the corpus's chord released as its top finger leaves:
    // the holders die EXACTLY where the glide departs, before it is in flight, so the chord sounded
    // whole for its whole tenure and stays a box that closes at the release.
    const std::vector<ChartNote> released = streamOf({
        travellingAt(
            noteAt(1, Fraction{}, 1, 7, Fraction{9, 8}),
            {{Fraction{5, 8}, 7}, {Fraction{3, 4}, 16}}),
        noteAt(1, Fraction{}, 2, 9, Fraction{5, 8}),
        noteAt(1, Fraction{}, 3, 9, Fraction{5, 8}),
    });
    const ChartShapes held_whole = deriveFrom(released);
    REQUIRE(held_whole.shapes.size() == 1);
    CHECK(held_whole.shapes[0].sustain == Fraction{5, 8});
    CHECK_FALSE(held_whole.shapes[0].sounds_in_parts);
    CHECK_FALSE(arpeggiosFrom(released)[0]);
    everySpanIsPositive(held_whole);
}

// LAW A at the join: a ring outliving the span that recorded it is still evidence, so a strike
// stating another stop on that string contradicts it.
TEST_CASE("A foreign sounding ring contradicts a slot that restates its string", "[core][chart]")
{
    // The figure that makes a ring foreign, which is the only thing that can: three plucks
    // accumulate into a span, a tap on string one at a fret BELOW the grip's 5 BREAKS the grip
    // (THE TAP'S FLOOR: the finger holding 5 is gone), and string one's first ring goes on
    // sounding past that break with no span recording it. It ends exactly at the beat that
    // restates the string. Three plucks rather than two because two accumulating members open no
    // span for the tap to break.
    const auto figure = [](const int restated_fret) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
            noteAt(2, Fraction{1, 2}, 3, 9, Fraction{7, 8}),
            tapAt(3, Fraction{}, 1, 3, Fraction{1, 128}),
            inMeasure(2, noteAt(1, Fraction{}, 1, restated_fret, Fraction{1, 4})),
        });
    };

    SECTION("A CONTRADICTION over a foreign ring closes an accumulation")
    {
        // The tap sounds below the 5 the string is still sounding: the finger moved, whatever the
        // grown span knows. Absorption is refused and the span closes at that beat.
        const ChartShapes derived = deriveFrom(figure(8));

        // The accumulation, ended at the tap's own onset. The tap's slot founds nothing over the
        // rings going on past that close: a right-hand onset states no stop, so founding is never
        // asked there (shape rules 1 and 7). The restatement a measure later does state one, but
        // it is a lone fresh note over strings 2 and 3 struck behind the close, so it founds
        // nothing either (shape rule 1): the accumulation is the figure's only span.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        // Two quarters: the close falls at the tap's own onset. It publishes no head, because the
        // fretting hand strikes nothing there.
        CHECK_FALSE(derived.shapes[0].closing_onset_place.has_value());
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        // It prints the grip its own members struck, never the fret the contradiction takes.
        CHECK(derived.postures[derived.shapes[0].posture].stops[0] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }

    SECTION("Neither a restated stop nor a moved finger founds over the rings past the close")
    {
        // The control the arm above needs: the accumulation closed at the tap, so the
        // restatement arrives with no span standing to join OR split. Whether it restates the 5
        // or moves the finger to 8, it is one fresh statement over rings struck behind the close,
        // and those count toward no founding (shape rule 1). Both figures derive the one
        // accumulation, identically.
        const ChartShapes moved = deriveFrom(figure(8));
        const ChartShapes derived = deriveFrom(figure(5));
        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(moved.shapes.size() == 1);
        CHECK(derived.shapes[0].place == moved.shapes[0].place);
        CHECK(derived.shapes[0].sustain == moved.shapes[0].sustain);
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        everySpanIsPositive(derived);
    }

    SECTION("A span OPENING over a foreign ring is untouched")
    {
        // Nothing is contradicted until a span STATES the string, so the law is scoped to the join
        // and never to the open. String one's first ring ends exactly where the chord that follows
        // restates the string at another fret, and that chord opens its shape as any chord does.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 7, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        // One span, stated whole by the chord that opens it.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 2});
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        CHECK(derived.postures[derived.shapes[0].posture].stops[0] == std::optional{fretStop(8)});
        CHECK(derived.postures[derived.shapes[0].posture].stops[1] == std::optional{fretStop(7)});
        everySpanIsPositive(derived);
    }

    SECTION("A SLIDE-OUT ring asserts no grip at its junction")
    {
        // The GP import figure law's own exemption, mirrored at read: by the ring's end the gliding
        // finger is off the board, so the fret the channel last stated is no standing grip and the
        // DISPLACEMENT witness stays silent at the junction — that silence is this section's
        // subject. The span still splits, at the tap, through THE TAP'S FLOOR rather than the
        // ring.
        std::vector<ChartNote> notes = figure(8);
        setSlideOut(notes[0], -1);
        const ChartShapes derived = deriveFrom(notes);

        // The first section's picture: the accumulation closes at the tap all the same, and the
        // strike a measure later founds nothing over the two rings still sounding beside it
        // (shape rule 1).
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
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
        noteAt(1, Fraction{}, 1, 5, Fraction{1}),
        noteAt(2, Fraction{}, 2, 7, Fraction{1, 4}),
        tapAt(3, Fraction{}, 1, 3, Fraction{1, 128}),
        inMeasure(2, noteAt(1, Fraction{}, 1, 8, Fraction{1, 4})),
    });
    const ChartShapes derived = deriveFrom(notes);

    // No slot in the figure musters three members, so nothing opens and nothing emits — the
    // contradiction has no span to split and manufactures none.
    CHECK(derived.shapes.empty());
}

// THE DATING CLAMP (LAW A's second half). The join refusal can only fire against a STANDING span,
// and the reel figure musters its opening minimum only AFTER the junction — so unclamped the span
// would open later and back-date its front across the instant the contradicted string audibly held
// another stop, and the fronted bracket would claim a grip that did not yet exist there. The
// foreign-sound floor bounds the dating instead: a member whose statement began behind the end of
// any stated string's foreign sound is clamped to it, and the span dates from the junction — the
// displacement case of the general bound the next case reaches through gaps of silence.
TEST_CASE(
    "A contradiction reopens the figure, and what was struck before it is carried", "[core][chart]")
{
    // String one sounds fret 7 up to beat 4, where a restrike replaces or restates it; string two's
    // long ring and the strike in measure 2 are the figure's other members.
    const auto figure = [](const int junction_fret) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 7, Fraction{3, 4}),
            noteAt(2, Fraction{}, 2, 5, Fraction{1}),
            noteAt(4, Fraction{}, 1, junction_fret, Fraction{1, 2}),
            inMeasure(2, noteAt(1, Fraction{}, 3, 5, Fraction{1, 2})),
        });
    };

    SECTION("a different fret breaks the span, and the figure after it has too few of its own")
    {
        // The 6 contradicts the 7 and breaks the span the two first notes were accumulating; it
        // opens a new span at its own strike, into which string two's 5 is carried, sounding there
        // since before the front. Carried members found nothing, so the two statements made at or
        // after the front — the 6 and the measure-2 strike — are under the thresholds, and nothing
        // is published (shape rule 1).
        CHECK(deriveFrom(figure(6)).shapes.empty());
    }

    SECTION("a same-fret restrike renews the grip, and the span runs from the chain's start")
    {
        // The control, differing only by the restated fret: the 7 restruck exactly at its own
        // ring's end re-sounds a string of a two-stop grip, so the span stands, and the measure-2
        // strike founds it from beat 1.
        const ChartShapes derived = deriveFrom(figure(7));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        everySpanIsPositive(derived);
    }
}

// THE HOLD-UNDER LAW, in the one shape it still has: a finger ADDED above a stop A STANDING SPAN
// ALREADY GRIPS contradicts nothing, and the stop RE-EMERGING under it as that finger lifts lifts
// nothing. A pull-off proves a finger on its landing stop at the RELEASE and at no earlier
// instant, so the exemption is a relation between the landing stop and the grip in force under the
// source — never a property of the source alone, and never a lone ring, which is no grip. The
// Torn-intro shape: an accumulation-founded grip whose rings sound into one another, then an
// ornament struck above the grip on one of its strings and pulled back off to it. The ornament
// figure's two attacks are the discriminating variables — everything else in every section is
// identical.
TEST_CASE("A legato source above the gripped stop never seams", "[core][chart]")
{
    // The grip: strings 6/5/4 accumulating (three members founds the span), string 4's ring
    // dying exactly at the ornament's onset. The ornament: string 4 fret 7 over the grip's 5,
    // then the release back at 3:1/2.
    const auto figure = [](ChartNote ornament, ChartNote release) {
        return streamOf({
            noteAt(1, Fraction{}, 6, 7, Fraction{3, 4}),
            noteAt(1, Fraction{1, 2}, 5, 5, Fraction{5, 8}),
            noteAt(2, Fraction{}, 4, 5, Fraction{1, 4}),
            std::move(ornament),
            std::move(release),
        });
    };
    const ChartNote ornament = noteAt(3, Fraction{}, 4, 7, Fraction{1, 8});

    SECTION("the pulled-off ornament rides the span whole")
    {
        const ChartShapes derived =
            deriveFrom(figure(ornament, pullOffAt(3, Fraction{1, 2}, 4, 5, Fraction{1, 8})));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        // The whole figure is one statement: the reach runs to the last ring and NOT past it
        // (the law extends no span beyond its sounding evidence).
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        // The posture prints the GRIP, not the ornament: the growth write is untouched by the
        // law, and the release's restatement leaves the grip's own fret standing.
        CHECK(derived.postures[derived.shapes[0].posture].stops[3] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }

    // The riff chugs the grip WHOLE and the ornament arrives inside a stroke: the rest of the grip
    // is restruck beside the finger added above. The stroke still says the whole grip — the source
    // states the held stop, its fret is the ornament — so the chord span rides it and stays a box
    // (read the other way, a picked open string under 4 pulled off with 6 restruck beside it would
    // seam there and open an arpeggio).
    SECTION("an ornament co-struck with the rest of the grip rides the chord span as a box")
    {
        for (const int held : {0, 2})
        {
            const std::vector<ChartNote> notes = streamOf({
                noteAt(1, Fraction{}, 2, 6, Fraction{1, 4}),
                noteAt(1, Fraction{}, 3, held, Fraction{1, 4}),
                noteAt(2, Fraction{}, 2, 6, Fraction{1, 4}),
                noteAt(2, Fraction{}, 3, 4, Fraction{1, 4}),
                pullOffAt(3, Fraction{}, 3, held, Fraction{1, 4}),
            });
            const ChartShapes derived = deriveFrom(notes);

            REQUIRE(derived.shapes.size() == 1);
            CHECK(derived.shapes[0].place == Fraction{});
            // Through the ornament's release, where the restruck 6 stops ringing.
            CHECK(derived.shapes[0].sustain == Fraction{1, 2});
            CHECK(
                derived.postures[derived.shapes[0].posture].stops[2] ==
                std::optional{fretStop(held)});
            CHECK(arpeggiosFrom(notes) == std::vector<bool>{false});
        }
    }

    SECTION("the same figure without the pull-off seams — the law reads the connection")
    {
        // One attack apart: the release is a plain strike, so nothing plants and the ornament's
        // foreign fret breaks the grip.
        const ChartShapes derived =
            deriveFrom(figure(ornament, noteAt(3, Fraction{1, 2}, 4, 5, Fraction{1, 8})));

        REQUIRE_FALSE(derived.shapes.empty());
        CHECK(derived.shapes[0].sustain < Fraction{3, 4});
    }

    SECTION("a hammer above the source plants nothing and the seam stands")
    {
        const ChartShapes derived =
            deriveFrom(figure(ornament, pullOffAt(3, Fraction{1, 2}, 4, 9, Fraction{1, 8})));

        REQUIRE_FALSE(derived.shapes.empty());
        CHECK(derived.shapes[0].sustain < Fraction{3, 4});
    }

    SECTION("a source gliding through its destination plants nothing")
    {
        // The traveled-range refusal, through the ride's own predicate: the ornament starts at 3
        // and glides to 7, sweeping the destination 5.
        ChartNote gliding = noteAt(3, Fraction{}, 4, 3, Fraction{1, 8});
        gliding.keyframes = {Keyframe{.offset = Fraction{1, 16}, .fret = 7}};
        const ChartShapes derived = deriveFrom(
            figure(std::move(gliding), pullOffAt(3, Fraction{1, 2}, 4, 5, Fraction{1, 8})));

        REQUIRE_FALSE(derived.shapes.empty());
        CHECK(derived.shapes[0].sustain < Fraction{3, 4});
    }

    SECTION("THE FOLD: a source over ground its string was not at fronts the span at its RELEASE")
    {
        // The figure opens on the source itself, so nothing was ever down on its string: the
        // source states the fret it SOUNDS and the release is an ordinary new statement, dated at
        // its own onset. The fold has no hop to make here — there is no earlier instant the
        // statement of 5 can be proven at — and the span therefore fronts an eighth in, exactly
        // as it would were the two notes plainly picked.
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 4, 7, Fraction{1, 8}),
            pullOffAt(1, Fraction{1, 2}, 4, 5, Fraction{3, 4}),
            noteAt(2, Fraction{}, 5, 5, Fraction{1, 2}),
            noteAt(2, Fraction{1, 2}, 6, 5, Fraction{1, 2}),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 8});
        CHECK(derived.shapes[0].sustain == Fraction{5, 8});
        everySpanIsPositive(derived);
    }
}

// A pull-off source over ground its string never held states the fret it sounds, so the figure
// breaks at the release exactly as the same notes plainly picked.
TEST_CASE("A source over ground its string never held states the fret it sounds", "[core][chart]")
{
    // THE SLIDE FIGURE: the hand plays the 7/8 position with nothing beneath it, then a 7 arrives
    // whose pull-off ends on 5. Nothing says a finger was on 5 while that 7 sounded — a pull-off
    // proves its landing stop AT THE RELEASE and at no earlier instant — so the source states the
    // 7 IT SOUNDS, which is the very stop the span already holds, and the figure derives exactly
    // as the same notes plainly picked: the break falls at the RELEASE, where a stop the span
    // never held is stated for the first time.
    SECTION("the release breaks the up-position span and founds nothing over its rings")
    {
        // The figure as sighted carried an OPEN sixth string beneath the 7/8 position; the stream
        // below carries a fretted 3 there instead, and an open string is a member exactly as a
        // fretted one is, which makes the two one case. The released 5 is a new statement judged
        // fresh, but a lone one over the scaffold's rings, which were struck behind the break, so
        // it founds nothing (shape rule 1) whichever stop the scaffold holds. The rule this case
        // is ABOUT is where the break falls, which is what the up-position frame below records.
        const ChartShapes open_scaffold = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 4, 7, Fraction{3, 4}),
            noteAt(1, Fraction{}, 5, 8, Fraction{3, 4}),
            noteAt(1, Fraction{}, 6, 0, Fraction{3, 4}),
            noteAt(2, Fraction{}, 4, 7, Fraction{1, 8}),
            noteAt(3, Fraction{}, 4, 7, Fraction{1, 8}),
            pullOffAt(3, Fraction{1, 2}, 4, 5, Fraction{1, 4}),
        }));
        REQUIRE(open_scaffold.shapes.size() == 1);
        CHECK(open_scaffold.shapes[0].place == Fraction{});
        CHECK(open_scaffold.shapes[0].sustain == Fraction{5, 8});
        everySpanIsPositive(open_scaffold);

        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 4, 7, Fraction{3, 4}),
            noteAt(1, Fraction{}, 5, 8, Fraction{3, 4}),
            noteAt(1, Fraction{}, 6, 3, Fraction{3, 4}),
            noteAt(2, Fraction{}, 4, 7, Fraction{1, 8}),
            noteAt(3, Fraction{}, 4, 7, Fraction{1, 8}),
            pullOffAt(3, Fraction{1, 2}, 4, 5, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        // Two and a half quarters to the break: the source restates the 7 the span already holds
        // and carries it, so the span runs to the RELEASE'S own onset and not to the source's.
        CHECK(derived.shapes[0].sustain == open_scaffold.shapes[0].sustain);
        // The up-position frame stays whole: no landing 5 ever reaches it, over the fretted
        // scaffold exactly as over the open one.
        CHECK(derived.postures[derived.shapes[0].posture].stops[3] == std::optional{fretStop(7)});
        everySpanIsPositive(derived);
    }

    SECTION("a grip already holding the plant rides — the accounted figure never breaks")
    {
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 4, 5, Fraction{3, 4}),
            noteAt(1, Fraction{}, 5, 8, Fraction{3, 4}),
            noteAt(1, Fraction{}, 6, 0, Fraction{3, 4}),
            noteAt(3, Fraction{}, 4, 7, Fraction{1, 8}),
            pullOffAt(3, Fraction{1, 2}, 4, 5, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        // The ornament states the grip it plants, so the frame never flickers to 7.
        CHECK(derived.postures[derived.shapes[0].posture].stops[3] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }
}

// THE CO-STRUCK SOURCE (the open-chord intro): a pull-off's source struck TOGETHER with a member
// that rings on past the release — the let-ring texture the importer stores. The source's string
// was at 3 when it spoke and nowhere near the stop it falls to, so nothing about the figure is a
// finger added above a grip: the source states the 3 IT SOUNDS, its release is an ordinary new
// statement, and the whole figure derives EXACTLY AS THE SAME NOTES PLAINLY PICKED. The stroke
// stands as the chord it struck and the release splits it chord -> parts — an eighth-note box at
// the stroke, then the parts figure the release founds.
//
// Dating the span back to the source instead would assert a hold-under exemption with no evidence
// for it, since the source's landing stop was never held. The corpus figure's destination is the
// OPEN string, which derives like every fret; its fretted twin is here to prove the two are one
// case. Each section states the law as an EQUALITY against its own plainly
// restruck control rather than writing one set of numbers out twice.
TEST_CASE(
    "A co-struck source over foreign ground derives as the same notes picked", "[core][chart]")
{
    // The stroke: string 2 at 3 ringing two quarters, string 5 at 3 for an eighth; string 5
    // releases to `landed` and rings on while the open strings arrive beneath the figure.
    const auto figure = [](const int landed, ChartNote release) {
        return streamOf({
            noteAt(1, Fraction{}, 2, 3, Fraction{1, 2}),
            noteAt(1, Fraction{}, 5, 3, Fraction{1, 8}),
            std::move(release),
            noteAt(2, Fraction{}, 4, landed, Fraction{3, 8}),
            noteAt(2, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
        });
    };

    SECTION("THE SIGHTED FIGURE: the pull onto the open string derives as the plain restrike")
    {
        const std::vector<ChartNote> notes =
            figure(0, pullOffAt(1, Fraction{1, 2}, 5, 0, Fraction{1, 2}));
        derivesLike(notes, figure(0, noteAt(1, Fraction{1, 2}, 5, 0, Fraction{1, 2})));

        // The numbers once, on the figure itself, so the equality above has something to mean: the
        // whole-grip box at the stroke, then the parts figure the open strings accumulate, fronted
        // at the release that begins it.
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        CHECK(derivedStops(derived, 0)[1] == std::optional{fretStop(3)});
        CHECK(derivedStops(derived, 0)[4] == std::optional{fretStop(3)});
        CHECK(derived.shapes[1].place == Fraction{1, 8});
        // String 2's fretted 3, struck in the box, still rings at the parts figure's front, so it
        // joins that grip as a carried member (shape rule 1) beside the three open strings and
        // bounds it: the piece holding it runs out where string 2 stops ringing.
        CHECK(derived.shapes[1].sustain == Fraction{3, 8});
        CHECK(derived.shapes[1].sounds_in_parts);
        const std::vector<std::optional<ChartStop>>& frets = derivedStops(derived, 1);
        CHECK(frets[1] == std::optional{fretStop(3)});
        CHECK(frets[2] == std::optional{fretStop(0)});
        CHECK(frets[3] == std::optional{fretStop(0)});
        CHECK(frets[4] == std::optional{fretStop(0)});
        // Its death splits rather than closes: the open strings run on alone, the arpeggio they
        // accumulated.
        CHECK(derived.shapes[2].place == Fraction{1, 2});
        CHECK(derived.shapes[2].sustain == Fraction{1, 8});
        CHECK(memberCount(derived, 2) == 3);
        CHECK_FALSE(derivedStops(derived, 2)[1].has_value());
        CHECK(derived.shapes[2].sounds_in_parts);
        const std::vector<bool> classes = arpeggiosFrom(notes);
        REQUIRE(classes.size() == 3);
        CHECK_FALSE(classes[0]);
        CHECK(classes[1]);
        CHECK(classes[2]);
        everySpanIsPositive(derived);
    }

    SECTION("the fretted twin derives identically — zero is a fret like any other")
    {
        derivesLike(
            figure(1, pullOffAt(1, Fraction{1, 2}, 5, 1, Fraction{1, 2})),
            figure(1, noteAt(1, Fraction{1, 2}, 5, 1, Fraction{1, 2})));
    }

    SECTION("a co-member HAMMERING away derives alike too — the direction changes nothing")
    {
        // One direction apart from the sighted figure, and the answer is the same: the connection
        // runs UP, so it never stated a stop beneath the source either, and the hammer breaks the
        // whole-grip chord exactly as a plain restrike at that fret does.
        const std::vector<ChartNote> notes =
            figure(0, pullOffAt(1, Fraction{1, 2}, 5, 5, Fraction{1, 2}));
        derivesLike(notes, figure(0, noteAt(1, Fraction{1, 2}, 5, 5, Fraction{1, 2})));

        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() >= 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        everySpanIsPositive(derived);
    }
}

// AN OPEN RING OUTSIDE THE GRIP IS FOREIGN SOUND (shape rule 3): no finger holds it, so a pull-off
// source struck over an open string that rings outside the grip displaces it exactly as the same
// fret plainly picked does, and its pull-off onto the open string bridges nothing. Riding it
// instead, the source would grow the standing grip, or date a span it founds back across the open
// ring, and the bracket would print its fret over a stretch in which the string rang open. The open
// ring here is struck with the first chord; still sounding where the second chord founds, it joins
// that grip as a carried member (shape rule 1), which is the first section's subject.
TEST_CASE("A pull-off source over an open ring outside the grip displaces it", "[core][chart]")
{
    // The first chord on strings 4 and 5, with string 3's open ring struck beside it and still
    // sounding after it closes; `later` follows.
    const auto figure = [](std::vector<ChartNote> later) {
        later.push_back(noteAt(1, Fraction{}, 4, 3, Fraction{1, 4}));
        later.push_back(noteAt(1, Fraction{}, 5, 5, Fraction{1, 4}));
        later.push_back(noteAt(1, Fraction{}, 3, 0, Fraction{1, 2}));
        return streamOf(std::move(later));
    };

    SECTION("over the open ring the chord's grip holds, the source rides it; a plain fret breaks")
    {
        // The open ring still sounds where the chord founds, so it joins the chord's grip as a
        // carried member (shape rule 1) and is no longer outside it. A pull-off source then rides
        // that open entry (the hold-under law, shape rule 7): its grip statement is the 0 the
        // carried member holds, so it restrikes the member at the SAME stop, making it the span's
        // own, and the span runs on through the figure to the release's end. The same fret plainly
        // picked strikes ANOTHER stop exactly where the carried 0 dies, so the span splits there
        // first (shape rule 1). The strike then displaces the dead 0 and breaks the remainder at
        // its own front, so the remainder publishes nothing; and a lone note over rings struck
        // behind the frontier founds nothing, so neither the 2 nor its release brackets anything.
        const std::vector<ChartNote> chord{
            noteAt(2, Fraction{}, 1, 5, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1}),
        };
        const auto with = [&chord](ChartNote source, ChartNote release) {
            std::vector<ChartNote> later = chord;
            later.push_back(std::move(source));
            later.push_back(std::move(release));
            return later;
        };
        const ChartShapes ridden = deriveFrom(figure(with(
            noteAt(3, Fraction{}, 3, 2, Fraction{1, 8}),
            pullOffAt(3, Fraction{1, 2}, 3, 0, Fraction{1, 4}))));
        CHECK(ridden.shapes.size() == 2);
        REQUIRE(ridden.shapes.size() >= 2);
        CHECK(ridden.shapes[1].place == Fraction{1, 4});
        CHECK(ridden.shapes[1].sustain == Fraction{5, 8});
        CHECK(derivedStops(ridden, 1)[2] == std::optional{fretStop(0)});
        everySpanIsPositive(ridden);

        const ChartShapes picked = deriveFrom(figure(with(
            noteAt(3, Fraction{}, 3, 2, Fraction{1, 8}),
            noteAt(3, Fraction{1, 2}, 3, 0, Fraction{1, 4}))));
        REQUIRE(picked.shapes.size() == 2);
        CHECK(picked.shapes[1].place == Fraction{1, 4});
        CHECK(picked.shapes[1].sustain == Fraction{1, 4});
        CHECK(picked.shapes[1].sounds_in_parts);
        // The displacing strike stands at the close, so the piece keeps its distance from it.
        CHECK(picked.shapes[1].closing_onset_place == std::optional{Fraction{1, 2}});
        CHECK(derivedStops(picked, 1)[2] == std::optional{fretStop(0)});
        everySpanIsPositive(picked);
    }

    SECTION("two opens over the old open ring are founded by the source riding that ring")
    {
        // Two open strings arrive one after the other beside string 3's open ring, which was struck
        // with the first chord and is carried into the span the first open opens: carried, it
        // founds nothing, and the two opens alone are under the minimum. The source on string 3
        // rides the carried open entry (the hold-under law, shape rule 7), restating it and making
        // it the span's own — the third own member sounding, which founds the span from the first
        // open's strike with the grip of three open strings. The release back to 0 restates it,
        // and the span runs to that ring's end.
        const std::vector<ChartNote> notes = figure({
            noteAt(2, Fraction{}, 1, 0, Fraction{1}),
            noteAt(2, Fraction{1, 2}, 2, 0, Fraction{1}),
            noteAt(3, Fraction{}, 3, 2, Fraction{1, 8}),
            pullOffAt(3, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        CHECK(derived.shapes[1].sustain == Fraction{5, 8});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(derivedStops(derived, 1)[0] == std::optional{fretStop(0)});
        CHECK(derivedStops(derived, 1)[1] == std::optional{fretStop(0)});
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(0)});
        everySpanIsPositive(derived);
    }

    SECTION("a source founding a span dates its front no earlier than the open ring's end")
    {
        // The same figure with string 3's open ring ending at the second open's onset, so the
        // two opens alone found nothing and the source is the third member sounding: its slot
        // founds the span. Its front may date back to the first open's statement only where
        // string 3 rang nothing foreign since, and string 3 rang open up to the second open.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 4, 3, Fraction{1, 4}),
            noteAt(1, Fraction{}, 5, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 3, 0, Fraction{3, 8}),
            noteAt(2, Fraction{}, 1, 0, Fraction{1}),
            noteAt(2, Fraction{1, 2}, 2, 0, Fraction{1}),
            noteAt(3, Fraction{}, 3, 2, Fraction{1, 8}),
            pullOffAt(3, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() >= 2);
        CHECK(derived.shapes[1].place == Fraction{3, 8});
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(2)});
        everySpanIsPositive(derived);
    }

    SECTION("a source struck at the front, where the open ring ended, founds with the chord")
    {
        // The open ring ends exactly where the chord and the source strike together, so it rang
        // nothing foreign inside the span and the source joins the grip it founds with.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 4, 3, Fraction{1, 4}),
            noteAt(1, Fraction{}, 5, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 3, 0, Fraction{1, 4}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 2}),
            noteAt(2, Fraction{}, 2, 7, Fraction{1, 2}),
            noteAt(2, Fraction{}, 3, 2, Fraction{1, 8}),
            pullOffAt(2, Fraction{1, 2}, 3, 0, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() >= 2);
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        const std::vector<std::optional<ChartStop>>& stops = derivedStops(derived, 1);
        CHECK(stops[0] == std::optional{fretStop(5)});
        CHECK(stops[1] == std::optional{fretStop(7)});
        CHECK(stops[2] == std::optional{fretStop(2)});
        everySpanIsPositive(derived);
    }
}

// THE LAW AT ITS WIDEST, stated as an EQUALITY. A pull-off proves a finger on its landing stop at
// the RELEASE and at no earlier instant, so unless a span is ALREADY STANDING and gripping that
// stop on the string, a source states the fret it SOUNDS and its release is an ordinary new
// statement: the figure derives exactly as the same notes plainly picked, span for span. Each
// section asserts that equality rather than a set of numbers, because the numbers belong to the
// plain figure and writing them out twice is what lets two readings of one law drift apart.
//
// EACH IS ASKED TWICE, bare and over HISTORY, because the defect this pins is history-dependent: a
// string that had ever been fretted could read as still holding the stop it once held, so a
// long-dead strike on every string the figure uses must change nothing at all. The first section
// is the acceptance figure — the dyad at 18:3.
TEST_CASE(
    "A pull-off over ground its string was not at derives as the same notes picked",
    "[core][chart]")
{
    // The same notes a bar later, under a strike on every string they use whose ring died a full
    // quarter before any of them speaks. The strings are taken from the figure itself, so a case
    // that grows a string cannot forget to give it a history.
    const auto over_history = [](const std::vector<ChartNote>& notes) {
        std::vector<ChartNote> stream;
        std::vector<int> given;
        for (const ChartNote& note : notes)
        {
            if (std::ranges::find(given, note.string) == given.end())
            {
                given.push_back(note.string);
                stream.push_back(noteAt(1, Fraction{}, note.string, 12, Fraction{1, 32}));
            }
            stream.push_back(inMeasure(2, note));
        }
        return streamOf(std::move(stream));
    };
    // One figure's two forms: `pulled` swaps every release between a pull-off and the plain
    // restrike of the very same fret, which is the only variable any section here has.
    const auto release_at =
        [](const bool pulled, const int beat, const int string, const int fret) {
            return pulled ? pullOffAt(beat, Fraction{}, string, fret, Fraction{1, 4})
                          : noteAt(beat, Fraction{}, string, fret, Fraction{1, 4});
        };

    SECTION("a dyad pulled off together is two boxes, exactly as the picked dyad is")
    {
        const auto dyad = [&release_at](const bool pulled) {
            return std::vector<ChartNote>{
                noteAt(1, Fraction{}, 3, 9, Fraction{1, 4}),
                noteAt(1, Fraction{}, 4, 9, Fraction{1, 4}),
                release_at(pulled, 2, 3, 7),
                release_at(pulled, 2, 4, 7),
            };
        };
        derivesLike(streamOf(dyad(true)), streamOf(dyad(false)));
        derivesLike(over_history(dyad(true)), over_history(dyad(false)));

        // The acceptance figure's own answer, stated once: TWO BOXES, never one span dated back
        // across the 9 to a finger nothing had put on the 7 yet.
        const ChartShapes derived = deriveFrom(streamOf(dyad(true)));
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derivedStops(derived, 0)[2] == std::optional{fretStop(9)});
        CHECK(derivedStops(derived, 0)[3] == std::optional{fretStop(9)});
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(7)});
        CHECK(derivedStops(derived, 1)[3] == std::optional{fretStop(7)});
        const std::vector<bool> classes = arpeggiosFrom(streamOf(dyad(true)));
        REQUIRE(classes.size() == 2);
        CHECK_FALSE(classes[0]);
        CHECK_FALSE(classes[1]);
        everySpanIsPositive(derived);
    }

    SECTION("a chord's own member pulled off derives as the chord with that member repicked")
    {
        const auto chord = [&release_at](const bool pulled) {
            return std::vector<ChartNote>{
                noteAt(1, Fraction{}, 2, 5, Fraction{1, 2}),
                noteAt(1, Fraction{}, 3, 5, Fraction{1, 2}),
                noteAt(1, Fraction{}, 4, 9, Fraction{1, 4}),
                release_at(pulled, 2, 4, 7),
            };
        };
        derivesLike(streamOf(chord(true)), streamOf(chord(false)));
        derivesLike(over_history(chord(true)), over_history(chord(false)));
    }

    SECTION("a figure arriving under a standing ring derives alike")
    {
        const auto standing = [&release_at](const bool pulled) {
            return std::vector<ChartNote>{
                noteAt(1, Fraction{}, 2, 5, Fraction{1}),
                noteAt(1, Fraction{}, 3, 5, Fraction{1}),
                noteAt(2, Fraction{}, 4, 9, Fraction{1, 4}),
                release_at(pulled, 3, 4, 7),
            };
        };
        derivesLike(streamOf(standing(true)), streamOf(standing(false)));
        derivesLike(over_history(standing(true)), over_history(standing(false)));
    }

    SECTION("a LONE RING under the stroke is no grip, whatever it still sounds")
    {
        // FIGURE M. String 4's 7 rings alone into the stroke at beat two — a lone ring is not a
        // grip, and the stroke is what FOUNDS the span here, so it states the frets it STRIKES and
        // the box prints the 5 and the 9. Asked at both rings, because the ring is beside the
        // point: a 7 still sounding into the stroke and a 7 choked long before it derive alike.
        const auto lone_ring = [](const Fraction ring, const bool pulled) {
            return streamOf({
                noteAt(1, Fraction{}, 4, 7, ring),
                noteAt(2, Fraction{}, 3, 5, Fraction{1, 2}),
                noteAt(2, Fraction{}, 4, 9, Fraction{1, 8}),
                pulled ? pullOffAt(2, Fraction{1, 2}, 4, 7, Fraction{1, 2})
                       : noteAt(2, Fraction{1, 2}, 4, 7, Fraction{1, 2}),
            });
        };
        derivesLike(lone_ring(Fraction{1, 4}, true), lone_ring(Fraction{1, 4}, false));
        derivesLike(lone_ring(Fraction{1, 32}, true), lone_ring(Fraction{1, 32}, false));

        // The box the stroke says, stated once: a chord of 5 and 9, never one span dated back to
        // the ring and wearing a 7 nobody struck there.
        const ChartShapes derived = deriveFrom(lone_ring(Fraction{1, 4}, true));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 4});
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        CHECK(derivedStops(derived, 0)[2] == std::optional{fretStop(5)});
        CHECK(derivedStops(derived, 0)[3] == std::optional{fretStop(9)});
        everySpanIsPositive(derived);
    }

    SECTION("and under two PEDALS restruck beneath it, where the grip is said again every beat")
    {
        // The restatement path rather than the ring path: the two 5s are struck again on every
        // beat, so the span is carried by same-stop restatements while the figure moves above it.
        const auto pedals = [&release_at](const bool pulled) {
            return std::vector<ChartNote>{
                noteAt(1, Fraction{}, 2, 5, Fraction{1, 4}),
                noteAt(1, Fraction{}, 3, 5, Fraction{1, 4}),
                noteAt(2, Fraction{}, 2, 5, Fraction{1, 4}),
                noteAt(2, Fraction{}, 3, 5, Fraction{1, 4}),
                noteAt(2, Fraction{}, 4, 9, Fraction{1, 4}),
                release_at(pulled, 3, 4, 7),
                noteAt(3, Fraction{}, 2, 5, Fraction{1, 4}),
                noteAt(3, Fraction{}, 3, 5, Fraction{1, 4}),
            };
        };
        derivesLike(streamOf(pedals(true)), streamOf(pedals(false)));
        derivesLike(over_history(pedals(true)), over_history(pedals(false)));
    }
}

// THE PRESSED STOP IS THE GRIP under a harmonic played over one (\ref harmonicOverPressedStop):
// the node its head prints is MEASURED from that stop, so the pressed fret is the grip the figure
// needs, and a pull-off from such a harmonic never moves the bracket onto the finger the release
// derives beneath it. The plant stays true in the wide table the hold-under law derives — a finger
// really is waiting there, and the FRET-HAND POSITION derivation is what factors it in — but the
// bracket does not print it.
//
// ONE AUTHORITY (\ref gripStatement), so the HOLD-UNDER arms answer it too: a bridge between two
// stops is the claim that they are one statement of one hand, and such a harmonic states its
// pressed stop whatever waits beneath it. So its release is a NEW statement wherever it falls,
// even over ground the string is demonstrably already at — the one place an ordinary source would
// ride. The two families of case below are the span ENDING with the harmonic's ring (the fronting
// figure) and the span still standing when the release lands (the co-struck figure), and the
// answer is the pressed stop in both.
TEST_CASE("A harmonic over a pressed stop states that stop to the grip", "[core][chart]")
{
    // A whole-grip stroke: two plain strings and the harmonic on string 3, each ringing a quarter,
    // with the release landing exactly where the harmonic's ring ends — strict adjacency, which is
    // what resolves it as a pull. The stroke's span therefore closes AT the release, so what it
    // publishes is the harmonic's own statement and nothing stated after it.
    const auto figure = [](ChartNote sounded) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            std::move(sounded),
            pullOffAt(2, Fraction{}, 3, 3, Fraction{1, 2}),
        });
    };
    ChartNote artificial = noteAt(1, Fraction{}, 3, 5, Fraction{1, 4});
    artificial.harmonic_node = 17.0;

    SECTION("the posture holds the pressed fret while the wide table holds the plant")
    {
        const std::vector<ChartNote> notes = figure(artificial);
        const ChartResolutions resolutions = chartResolutions(notes, {}, 0, makeTempoMap());
        REQUIRE(resolutions.connections.legato[indexAt(notes, 1, 2, 3)] == LegatoMotion::Pull);
        // The plant is REAL, and the wide table says so: a finger waits on 3 for the whole of the
        // harmonic's ring, which is exactly what the hand window has to reach.
        CHECK(
            chartPlantedStops(resolutions.connections)[indexAt(notes, 1, 1, 3)] ==
            std::optional{3});

        const ChartShapes derived = deriveFrom(notes);
        REQUIRE_FALSE(derived.shapes.empty());
        CHECK(derivedStops(derived, 0)[2] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }

    // THE ONE DISCRIMINATION THE EXEMPTION STILL MAKES, and it needs ground the string is already
    // AT: string 3 states fret 3 on the downbeat and rings exactly into the source, which then
    // falls back to that very 3. An ORDINARY source there is a finger added above a stop the
    // string demonstrably holds, so it states the 3 and the figure never seams; the HARMONIC over
    // a pressed stop states its PRESSED FRET whatever waits beneath it, so the same figure BREAKS
    // at the node's own onset, where that 5 displaces the 3. Over any other ground the two forms
    // agree, which is why the discrimination has to be asked here or nowhere.
    const auto over_its_landing = [](ChartNote sounded) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{3, 4}),
            noteAt(1, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(1, Fraction{}, 3, 3, Fraction{1, 4}),
            std::move(sounded),
            pullOffAt(3, Fraction{}, 3, 3, Fraction{1, 4}),
        });
    };

    SECTION("an ordinary source over the stop its string already holds rides")
    {
        // One field apart from the section below: drop the node and the head prints the 5 it
        // sounds, the source states the 3 under it, and one span runs the whole figure.
        const ChartShapes derived =
            deriveFrom(over_its_landing(noteAt(2, Fraction{}, 3, 5, Fraction{1, 4})));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK(derivedStops(derived, 0)[2] == std::optional{fretStop(3)});
        everySpanIsPositive(derived);
    }

    SECTION("the harmonic over the same ground states its pressed fret and breaks the grip")
    {
        ChartNote touched = noteAt(2, Fraction{}, 3, 5, Fraction{1, 4});
        touched.harmonic_node = 17.0;
        const ChartShapes derived = deriveFrom(over_its_landing(std::move(touched)));

        // A quarter instead of the ordinary source's three: the pressed 5 contradicts the 3 the
        // grip states, so the span closes at the harmonic's own onset. The pressed 5 is a stop the
        // fretting hand states, but a lone one over the chord's two rings struck behind the close,
        // so it founds nothing (shape rule 1), and neither does the release that puts the 3 back.
        // The one span prints the 3, never the pressed 5 over it.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 4}});
        CHECK(derivedStops(derived, 0)[2] == std::optional{fretStop(3)});
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    // THE CO-STRUCK FIGURE, the reason the rule reaches the HOLD-UNDER arms as well. The same
    // harmonic, struck inside a chord that is STILL STANDING when the release lands: two plain 5s
    // ringing four quarters around it, the harmonic ringing one, and the release landing on its
    // plant at the far end of that ring. The span therefore does not end with the harmonic's own
    // ring, so the release is a statement made INSIDE it — and a hold-under law reading the plant
    // bare would call that release the span's own finger lifting and let it write 3 over the 5 in a
    // posture the bracket prints beside the head's standing 5.
    const auto co_struck = [](ChartNote sounded) {
        return streamOf({
            noteAt(1, Fraction{}, 3, 5, Fraction{1}),
            noteAt(1, Fraction{}, 5, 5, Fraction{1}),
            std::move(sounded),
            pullOffAt(2, Fraction{}, 4, 3, Fraction{1, 2}),
        });
    };
    // What the law yields once both arms ask the grip statement: the span founded at the harmonic's
    // onset keeps the PRESSED 5 on its string and CLOSES at the release's own onset (the close's
    // event arm). The release is then a lone fresh statement over the two chord strings struck
    // behind that close, so it founds nothing (shape rule 1): the 3 is never written over the 5.
    const auto closes_at_the_release = [](const std::vector<ChartNote>& notes) {
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derivedStops(derived, 0)[3] == std::optional{fretStop(5)});
        CHECK(derived.shapes[0].place == Fraction{});
        // A quarter from beat 1 at 4/4: the release's own onset.
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[0].closing_onset_place == Fraction{1, 4});
        everySpanIsPositive(derived);
    };

    SECTION("a release inside the span is a new statement, never a finger already down")
    {
        ChartNote inner = noteAt(1, Fraction{}, 4, 5, Fraction{1, 4});
        inner.harmonic_node = 17.0;
        closes_at_the_release(co_struck(std::move(inner)));
    }

    SECTION("a plain tap's release onto its own planted finger still rides")
    {
        // The hold-under law itself, untouched: a tap at 12 over a finger planted on 5, released
        // onto THAT 5. The tap states the 5 it plants, the release states the same 5, so the two
        // are one statement of one hand and the span rides through them whole — which is exactly
        // what the harmonic above is NOT, because it states the stop its node is measured from
        // instead of the finger waiting under it.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 3, 5, Fraction{1}),
            noteAt(1, Fraction{}, 5, 5, Fraction{1}),
            tapAt(1, Fraction{}, 4, 12, Fraction{1, 4}),
            pullOffAt(2, Fraction{}, 4, 5, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derivedStops(derived, 0)[3] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }
}

// A NATURAL HARMONIC STATES NO GRIP: a node touched with nothing pressed is
// no stop the hand holds, so it founds, grows and joins no span, and no posture holds a node. Its
// STRIKE still sounds, so the split a harmonic makes falls out of the ordinary contradiction law
// reading a stop that cannot say a node is fret 0 — node 5 is not fret 5, nor the open string it
// shares a fret number with — while a node on a string the span does not name leaves the span
// standing. Read as an open string instead, a natural harmonic would vanish into the grip and the
// corpus chart would run one span 9.5 quarters straight through a harmonic passage.
TEST_CASE("A natural harmonic states no grip, and its strike still splits", "[core][chart]")
{
    // The third partial's node as the importer stores it, so the fixture cannot drift from the
    // corpus: a whole-number label prints "7", the finger sits in fret 8.
    constexpr double third_partial = 7.01955;

    SECTION("THE SIGHTED FIGURE: natural harmonics split the span the open strings held")
    {
        // An open-chord texture accumulating through measure 1 into the standing grip
        // {s1:0, s2:3, s3:0, s4:0, s5:0, s6:0}, its rings clamped where their strings are next
        // struck; then the twelfth-partial chord on strings 4/5/6 at 2:2, the third-partial chord
        // on the same strings at 2:3.5, and at 3:1 the ordinary open-chord figure. THREE spans
        // where a node read as an open string gives one of 9.5 quarters: the texture closes AT the
        // node chord, the two node chords found nothing and wear no bracket (their heads print the
        // nodes), and the figure at 3:1 founds ordinarily and then splits at its own release, the
        // co-struck source having spoken over ground its string was never at. The parts figure
        // the release founds then splits once more where its carried string 2 dies, so the
        // figure derives four spans in all.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 0, Fraction{2}),
            noteAt(1, Fraction{}, 2, 3, Fraction{2}),
            noteAt(1, Fraction{1, 2}, 3, 0, Fraction{9, 4}),
            noteAt(2, Fraction{}, 4, 0, Fraction{1}),
            noteAt(2, Fraction{1, 2}, 5, 0, Fraction{7, 8}),
            noteAt(3, Fraction{}, 6, 0, Fraction{3, 4}),
            inMeasure(2, harmonicAt(2, Fraction{}, 4, 12.0, Fraction{3, 8})),
            inMeasure(2, harmonicAt(2, Fraction{}, 5, 12.0, Fraction{3, 8})),
            inMeasure(2, harmonicAt(2, Fraction{}, 6, 12.0, Fraction{3, 8})),
            inMeasure(2, harmonicAt(3, Fraction{1, 2}, 4, third_partial, Fraction{3, 8})),
            inMeasure(2, harmonicAt(3, Fraction{1, 2}, 5, third_partial, Fraction{3, 8})),
            inMeasure(2, harmonicAt(3, Fraction{1, 2}, 6, third_partial, Fraction{3, 8})),
            inMeasure(3, noteAt(1, Fraction{}, 2, 3, Fraction{1, 2})),
            inMeasure(3, noteAt(1, Fraction{}, 5, 3, Fraction{1, 8})),
            inMeasure(3, pullOffAt(1, Fraction{1, 2}, 5, 0, Fraction{1, 2})),
            inMeasure(3, noteAt(2, Fraction{}, 4, 0, Fraction{3, 8})),
            inMeasure(3, noteAt(2, Fraction{1, 2}, 3, 0, Fraction{1, 4})),
        });
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 4);
        // The texture, closed by the node chord's own onset: node 12 struck over the open strings
        // the grip holds is a contradiction on strings the span names, and no plant bridges a
        // node.
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{5, 4}});
        // Neither node chord founds a span — three co-struck naturals are no own stops, and the
        // rings sounding under them belong to the span they closed — so the next span is the
        // open-chord figure at 3:1, founded exactly as it is with no harmonics before it: the
        // co-struck 3 beside the source, which states the 3 IT SOUNDS over a string it was never
        // at. Its release therefore begins a statement of its own an eighth later, so the tail
        // figure is the box its stroke says and the parts span the release founds — the same two
        // spans the figure derives when the pull-off is a plain restrike.
        CHECK(derived.shapes[1].place == Fraction{2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        const std::vector<std::optional<ChartStop>>& stroke = derivedStops(derived, 1);
        CHECK(stroke[1] == std::optional{fretStop(3)});
        CHECK(stroke[4] == std::optional{fretStop(3)});
        // The open string still ringing from measure 1 joins the grip as a carried member (shape
        // rule 1), one the stroke did not strike, so the span sounds in parts (shape rule 5).
        CHECK(stroke[2] == std::optional{fretStop(0)});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(derived.shapes[2].place == Fraction{17, 8});
        // The stroke's fretted 3 still rings where the release founds the parts span, so it joins
        // that grip as a carried member and bounds it: its death where string 2 stops ringing
        // splits the parts span rather than closing it.
        CHECK(derived.shapes[2].sustain == Fraction{3, 8});
        const std::vector<std::optional<ChartStop>>& figure = derivedStops(derived, 2);
        CHECK(figure[1] == std::optional{fretStop(3)});
        CHECK(figure[2] == std::optional{fretStop(0)});
        CHECK(figure[3] == std::optional{fretStop(0)});
        CHECK(figure[4] == std::optional{fretStop(0)});
        CHECK(derived.shapes[2].sounds_in_parts);
        // The remainder runs on with the three opens alone, still an arpeggio: the hand's own
        // members accumulated, and that verdict survives the split.
        CHECK(derived.shapes[3].place == Fraction{5, 2});
        CHECK(derived.shapes[3].sustain == Fraction{1, 8});
        CHECK(memberCount(derived, 3) == 3);
        CHECK_FALSE(derivedStops(derived, 3)[1].has_value());
        CHECK(derived.shapes[3].sounds_in_parts);
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("a node 5 struck over a held fret 5 breaks the grip, never restates it")
    {
        // A grip holding a pressed 5 on string 3, then a natural at the FIFTH-fret node on that
        // string: the same printed number, a different place, so the grip breaks. The BREAK is the
        // whole proof — read as fret 5 the node restates the stop and the span rides through it —
        // and the node itself founds nothing over the rings it arrives over: it states no grip,
        // so founding is never asked at its slot (shape rules 1 and 7).
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{1, 2}, 2, 7, Fraction{7, 8}),
            noteAt(2, Fraction{}, 3, 5, Fraction{1, 4}),
            harmonicAt(3, Fraction{}, 3, 5.0, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 2}});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.postures[derived.shapes[0].posture].stops[2] == std::optional{fretStop(5)});
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("a node touch is not the open string it shares a fret number with")
    {
        // The grip holds string 4 OPEN; a natural at node 12 on that string sounds something other
        // than the open string the grip names, and the grip breaks at it. The BREAK is the whole
        // proof — read as the open string the node restates what the grip holds and the span
        // rides — and the node founds nothing over the rings still sounding: it states no grip,
        // so founding is never asked at its slot (shape rules 1 and 7).
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{1, 2}, 2, 7, Fraction{7, 8}),
            noteAt(2, Fraction{}, 4, 0, Fraction{1, 4}),
            harmonicAt(3, Fraction{}, 4, 12.0, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 2}});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("two harmonics at one node restate nothing: there is no node grip to restate")
    {
        // The twelfth-partial node on string 5 struck twice with overlapping rings, beside two
        // opens. Neither strike is a statement, so there is no node grip for the second to
        // restate, and the node's ring joins nothing: what remains is two open rings, short of the
        // three members sound alone needs to found a span.
        const ChartShapes derived = deriveFrom(streamOf({
            harmonicAt(1, Fraction{}, 5, 12.0, Fraction{1, 2}),
            noteAt(1, Fraction{1, 2}, 1, 0, Fraction{7, 8}),
            noteAt(2, Fraction{}, 2, 0, Fraction{3, 4}),
            harmonicAt(2, Fraction{1, 2}, 5, 12.0, Fraction{5, 8}),
        }));

        CHECK(derived.shapes.empty());
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("a harmonic chord founds nothing; the fretted twelves after it found their own span")
    {
        // Three co-struck naturals with nothing standing are no own stops, so they open no span
        // and wear no bracket — their heads print the nodes. Struck again after a rest, the second
        // chord founds nothing either; the fretted twelves after it are the one span, on a grip
        // of pressed frets.
        const ChartShapes derived = deriveFrom(streamOf({
            harmonicAt(1, Fraction{}, 4, 12.0, Fraction{1, 4}),
            harmonicAt(1, Fraction{}, 5, 12.0, Fraction{1, 4}),
            harmonicAt(1, Fraction{}, 6, 12.0, Fraction{1, 4}),
            harmonicAt(3, Fraction{}, 4, 12.0, Fraction{1, 4}),
            harmonicAt(3, Fraction{}, 5, 12.0, Fraction{1, 4}),
            harmonicAt(3, Fraction{}, 6, 12.0, Fraction{1, 4}),
            inMeasure(2, noteAt(1, Fraction{}, 4, 12, Fraction{1, 4})),
            inMeasure(2, noteAt(1, Fraction{}, 5, 12, Fraction{1, 4})),
            inMeasure(2, noteAt(1, Fraction{}, 6, 12, Fraction{1, 4})),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1});
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        const std::vector<std::optional<ChartStop>>& twelves = derivedStops(derived, 0);
        CHECK(twelves[3] == std::optional{fretStop(12)});
        CHECK(twelves[4] == std::optional{fretStop(12)});
        CHECK(twelves[5] == std::optional{fretStop(12)});
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("a harmonic on a string the grip neither states nor holds leaves the span unchanged")
    {
        // No "a node always breaks" rule was invented: a node struck on a string the span does not
        // name contradicts nothing, so the span stands — and, stating no grip, the node does not
        // grow it either. The span derives exactly as it does with no harmonic at all: its front,
        // its grip, which holds no node, and its extent — the node's ring outlives every fretted
        // member, so a node that joined the span would carry it past the control's close.
        const std::vector<ChartNote> grip{
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{1, 2}, 2, 7, Fraction{7, 8}),
            noteAt(2, Fraction{}, 3, 9, Fraction{3, 4}),
        };
        std::vector<ChartNote> chimed = grip;
        chimed.push_back(harmonicAt(3, Fraction{}, 6, 12.0, Fraction{3, 2}));
        const ChartShapes derived = deriveFrom(streamOf(std::move(chimed)));
        const ChartShapes control = deriveFrom(streamOf(grip));

        REQUIRE(derived.shapes.size() == 1);
        REQUIRE(control.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].place == control.shapes[0].place);
        CHECK(derived.shapes[0].sustain == control.shapes[0].sustain);
        CHECK(derived.shapes[0].closing_onset_place == control.shapes[0].closing_onset_place);
        CHECK(derivedStops(derived, 0) == derivedStops(control, 0));
        CHECK_FALSE(derivedStops(derived, 0)[5].has_value());
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("the scope guards: the picking hand's nodes and a pressed stop stay frets")
    {
        // A two-hand tap harmonic pressing 5 states that 5, never its node; an artificial harmonic
        // (the same pressed 5 under a node 17) states its pressed 5 alike. Neither touches the
        // grip, so the span that holds string 1 at 5 rides through both.
        ChartNote tapped = tapAt(2, Fraction{}, 1, 5, Fraction{1, 4});
        tapped.harmonic_node = 17.0;
        ChartNote artificial = noteAt(3, Fraction{}, 1, 5, Fraction{1, 4});
        artificial.harmonic_node = 17.0;
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{1, 2}, 2, 7, Fraction{7, 8}),
            noteAt(1, Fraction{1, 2}, 3, 9, Fraction{7, 8}),
            tapped,
            artificial,
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.postures[derived.shapes[0].posture].stops[0] == std::optional{fretStop(5)});
        everySpanIsPositive(derived);
    }
}

// A SPAN NEVER PRINTS A FRET OVER A HARMONIC'S TAIL: while a natural harmonic
// rings, its string is demonstrably not fretted, so for that whole tenure the string is no part of
// any grip statement. A bracket asserts its grip from its front, so a strike on a string the span
// does not state, whose last foreign sound ended STRICTLY after that front, breaks the span rather
// than joining it (Law A at the join) — exactly as it breaks where that sound still rings at the
// strike. Where the foreign ring ended is no difference in what the hand did.
TEST_CASE("A span never prints a fret over a harmonic's tail", "[core][chart]")
{
    // The sighted figure, by how long the chime rings: a natural at node 12 on string 4 at 1:1; a
    // fretted pair on strings 2 and 3 at 1:2 ringing to 1:5, which founds a span while the chime
    // may still ring; and a fret on string 4 at 1:4, ringing to 1:5 beside the pair.
    const auto figure = [](const Fraction chime) {
        return streamOf({
            harmonicAt(1, Fraction{}, 4, 12.0, chime),
            noteAt(2, Fraction{}, 2, 3, Fraction{3, 4}),
            noteAt(2, Fraction{}, 3, 2, Fraction{3, 4}),
            noteAt(4, Fraction{}, 4, 2, Fraction{1, 4}),
        });
    };

    SECTION("THE SIGHTED FIGURE: a fret struck after the chime died breaks the span")
    {
        // The chime rings to 1:3, a quarter past the span's 1:2 front and a quarter short of the
        // late fret. Joined by growth, the fret would make one posture holding strings 2, 3 AND 4,
        // running to the rings' end at 1:5, so the bracket would print string 4's 2 back to 1:2,
        // over the chime's tail. Instead the span closes at the fret's onset on strings 2 and 3
        // alone, and the fret founds nothing over the pair's rings still sounding, struck behind
        // that close (shape rule 1): its 2 prints in no bracket at all, never over the chime's
        // tail.
        const ChartShapes derived = deriveFrom(figure(Fraction{1, 2}));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 4});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{3, 4}});
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 0);
        CHECK(grip[1] == std::optional{fretStop(3)});
        CHECK(grip[2] == std::optional{fretStop(2)});
        CHECK_FALSE(grip[3].has_value());
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("THE EQUALITY EDGE: a chime ending exactly at the front leaves the string free")
    {
        // The chime ends AT the 1:2 front, so string 4 was free for every instant the bracket
        // covers and the late fret JOINS by growth: one span holding strings 2, 3 and 4, running
        // to the rings' end at 1:5. The join's boundary is strict — a sound ending at the front
        // is Law A's dating floor met, not crossed.
        const ChartShapes derived = deriveFrom(figure(Fraction{1, 4}));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 4});
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 0);
        CHECK(grip[1] == std::optional{fretStop(3)});
        CHECK(grip[2] == std::optional{fretStop(2)});
        CHECK(grip[3] == std::optional{fretStop(2)});
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("THE DISPLACEMENT CONTROL: a chime still ringing at the fret derives the same")
    {
        // The chime rings to 1:4, the late fret's own onset, so the strike displaces a sound
        // still audible there — Law A's foreign-ring break, read end-inclusively at the junction.
        // The join's break is that same break for a ring that ended sooner, so the two figures
        // derive identically.
        derivesLike(figure(Fraction{3, 4}), figure(Fraction{1, 2}));
        const ChartShapes derived = deriveFrom(figure(Fraction{3, 4}));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 4});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{3, 4}});
    }

    SECTION("THE OPEN-RING ANALOG: an open string sounding into the founding joins and splits it")
    {
        // The chime replaced by string 4 struck OPEN at 1:1 and ringing to 1:3 — struck inside a
        // box on strings 5 and 6 that closes at 1:2. Unlike the chime, the open ring is a stop
        // the grip can hold: still sounding where the pair founds, it joins the pair's grip as a
        // carried member (shape rule 1) and bounds it, so the piece holding it ends where it dies
        // at 1:3. That death is a split, not a close: the pair runs on from it, fronted there,
        // which is also the end of string 4's foreign sound, so the fret on string 4 at 1:4 grows
        // that remainder rather than breaking it. Its 2 prints from where the open string
        // stopped, never over it.
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 5, 3, Fraction{1, 4}),
            noteAt(1, Fraction{}, 6, 3, Fraction{1, 4}),
            noteAt(1, Fraction{}, 4, 0, Fraction{1, 2}),
            noteAt(2, Fraction{}, 2, 3, Fraction{3, 4}),
            noteAt(2, Fraction{}, 3, 2, Fraction{3, 4}),
            noteAt(4, Fraction{}, 4, 2, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        CHECK_FALSE(derived.shapes[1].closing_onset_place.has_value());
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 1);
        CHECK(grip[1] == std::optional{fretStop(3)});
        CHECK(grip[2] == std::optional{fretStop(2)});
        CHECK(grip[3] == std::optional{fretStop(0)});
        CHECK(derived.shapes[2].place == Fraction{1, 2});
        CHECK(derived.shapes[2].sustain == Fraction{1, 2});
        const std::vector<std::optional<ChartStop>>& late = derivedStops(derived, 2);
        CHECK(late[1] == std::optional{fretStop(3)});
        CHECK(late[2] == std::optional{fretStop(2)});
        CHECK(late[3] == std::optional{fretStop(2)});
        everySpanIsPositive(derived);
    }
}

// WHAT SOUNDS UNDER A BRACKET'S OPENING IS WHAT IT PRINTS (shape rule 1): once a span is founded on
// its own onsets, a ring struck before its front and still sounding there is part of the sounding
// shape whenever it was struck, open or fretted alike — a CARRIED member, which joins the grip,
// bounds the reach and makes the span sound in parts, and whose death splits the span rather than
// closing it. It never founds: founding counts only statements made since the coverage frontier,
// so rings a closed span left sounding found nothing under a lone strike. Founding is asked only
// where the fretting hand states a stop, so a natural harmonic's node founds nothing and its ring
// joins nothing (shape rules 1 and 7).
//
// The LANDING is the other seam a member crosses — the grip itself moving under fingers that
// slid — and there every member of the closing span still sounding survives, open strings
// included.
TEST_CASE("What sounds under a bracket's opening is what it prints", "[core][chart]")
{
    SECTION("an open-position arpeggio founds from its first open string")
    {
        // E0, then A2, then D2, each ringing on: at the D2 the three accumulate — dated at the
        // E0, with the 0 in the posture. The fretted control derives the identical shape, which
        // is the open string counting exactly as a fretted one does.
        const ChartShapes open_first = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 6, 0, Fraction{1}),
            noteAt(2, Fraction{}, 5, 2, Fraction{3, 4}),
            noteAt(3, Fraction{}, 4, 2, Fraction{1, 2}),
        }));
        REQUIRE(open_first.shapes.size() == 1);
        CHECK(open_first.shapes[0].place == Fraction{});
        CHECK(memberCount(open_first, 0) == 3);
        CHECK(derivedStops(open_first, 0)[5] == std::optional{fretStop(0)});

        const ChartShapes fretted = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 6, 3, Fraction{1}),
            noteAt(2, Fraction{}, 5, 2, Fraction{3, 4}),
            noteAt(3, Fraction{}, 4, 2, Fraction{1, 2}),
        }));
        REQUIRE(fretted.shapes.size() == 1);
        CHECK(fretted.shapes[0].place == open_first.shapes[0].place);
        CHECK(fretted.shapes[0].sustain == open_first.shapes[0].sustain);
        everySpanIsPositive(open_first);
    }

    SECTION("drones ringing out of a closed span found nothing under a lone melody note")
    {
        // Two drones under a melody: the first melody note founds on them, and the second
        // contradicts it and founds nothing over the drones still sounding — they were struck
        // behind the close, so a pedal under a melody brackets nothing (shape rule 1). The third
        // is struck TOGETHER with one drone, a restrike stating the same open stop: two stops at
        // one slot, a statement that founds on its own onsets. The drone that was not restruck
        // still sounds there, so it joins the grip as a carried member the pair did not strike,
        // and that span sounds in parts (shape rule 5).
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 6, 0, Fraction{2}),
            noteAt(1, Fraction{1, 2}, 5, 0, Fraction{15, 8}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 7, Fraction{1, 4}),
            noteAt(4, Fraction{}, 1, 8, Fraction{1, 4}),
            noteAt(4, Fraction{}, 6, 0, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 2}});
        CHECK(derived.shapes[1].place == Fraction{3, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        CHECK(memberCount(derived, 1) == 3);
        CHECK(derivedStops(derived, 1)[0] == std::optional{fretStop(8)});
        CHECK(derivedStops(derived, 1)[5] == std::optional{fretStop(0)});
        CHECK(derivedStops(derived, 1)[4] == std::optional{fretStop(0)});
        CHECK(derived.shapes[1].sounds_in_parts);
        everySpanIsPositive(derived);
    }

    SECTION("an older open ring founds nothing, and joins the span its picks found over it")
    {
        // A chord with a drone struck under it is one span; its fretted rings quit and the drone
        // rings on. A staggered figure then picks over it. The drone was struck behind the
        // chord's close, so it counts toward no founding (shape rule 1): the three picks found on
        // their own, at the third, dated at the first. Still sounding at that front, the drone
        // joins as a carried member and bounds the span, so its death splits it, and the picks
        // run on alone as the arpeggio they accumulated. The control has one pick fewer and so
        // founds nothing: the drone's count would have been its third member. A chord STRUCK over
        // the drone is the other arm — a statement — and the drone joins its grip as a carried
        // member the chord did not strike, so the span sounds in parts.
        const auto figure = [](const bool third_member, const Fraction drone_ring) {
            std::vector<ChartNote> notes{
                noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
                noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
                noteAt(1, Fraction{}, 6, 0, drone_ring),
                noteAt(3, Fraction{}, 1, 5, Fraction{3, 4}),
                noteAt(3, Fraction{1, 2}, 2, 7, Fraction{5, 8}),
            };
            if (third_member)
            {
                notes.push_back(noteAt(4, Fraction{}, 3, 9, Fraction{1, 2}));
            }
            return streamOf(std::move(notes));
        };

        const ChartShapes derived = deriveFrom(figure(true, Fraction{7, 8}));
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(memberCount(derived, 1) == 4);
        CHECK(derivedStops(derived, 1)[5] == std::optional{fretStop(0)});
        // The drone dies seven eighths into the bar, and as a carried member it splits the span
        // there: the picks run on alone, still an arpeggio, to their own rings' end.
        CHECK(derived.shapes[1].sustain == Fraction{3, 8});
        CHECK(derived.shapes[2].place == Fraction{7, 8});
        CHECK(derived.shapes[2].sustain == Fraction{3, 8});
        CHECK(memberCount(derived, 2) == 3);
        CHECK_FALSE(derivedStops(derived, 2)[5].has_value());
        CHECK(derived.shapes[2].sounds_in_parts);
        everySpanIsPositive(derived);

        const ChartShapes control = deriveFrom(figure(false, Fraction{7, 8}));
        REQUIRE(control.shapes.size() == 1);
        CHECK(control.shapes[0].sustain == Fraction{1, 4});

        std::vector<ChartNote> grown = figure(true, Fraction{2});
        grown.push_back(inMeasure(2, noteAt(1, Fraction{}, 6, 3, Fraction{1, 4})));
        const ChartShapes replaced = deriveFrom(streamOf(std::move(grown)));
        // A fret struck on the drone's string DISPLACES the open sound there (Law A reads the
        // sound, member or not, exactly as for the harmonic chord over ringing opens below): the
        // span holding the carried drone ends at the strike, and nothing stated before it runs on
        // past it to print the new stop from a front before which the string audibly rang open.
        // The strike is then a lone note over rings struck behind that close, so it founds
        // nothing (shape rule 1), and no span of zero length stands in its place.
        REQUIRE(replaced.shapes.size() == 2);
        CHECK(replaced.shapes[1].place == Fraction{1, 2});
        CHECK(replaced.shapes[1].sustain == Fraction{1, 2});
        CHECK(derivedStops(replaced, 1)[5] == std::optional{fretStop(0)});
        everySpanIsPositive(replaced);

        // The chord over the drone: struck at beat three while the drone rings on from the closed
        // span, the drone joins its grip as a carried member, one the chord did not strike, so the
        // span sounds in parts (shape rule 5); it dates from the chord's own stroke either way.
        // Without the drone it is a box by its own stroke.
        const auto chord_over = [](const bool drone) {
            std::vector<ChartNote> notes{
                noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
                noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
                noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
                noteAt(3, Fraction{}, 2, 7, Fraction{1, 4}),
            };
            if (drone)
            {
                notes.push_back(noteAt(1, Fraction{}, 6, 0, Fraction{2}));
            }
            return streamOf(std::move(notes));
        };
        const ChartShapes over_drone = deriveFrom(chord_over(true));
        REQUIRE(over_drone.shapes.size() == 2);
        CHECK(over_drone.shapes[1].sounds_in_parts);
        CHECK(over_drone.shapes[1].place == Fraction{1, 2});
        CHECK(over_drone.shapes[1].sustain == Fraction{1, 4});
        CHECK(derivedStops(over_drone, 1)[5] == std::optional{fretStop(0)});
        const ChartShapes bare = deriveFrom(chord_over(false));
        REQUIRE(bare.shapes.size() == 2);
        CHECK_FALSE(bare.shapes[1].sounds_in_parts);
        CHECK(bare.shapes[1].place == Fraction{1, 2});
        CHECK(derivedStops(over_drone, 1)[0] == derivedStops(bare, 1)[0]);
        CHECK(derivedStops(over_drone, 1)[1] == derivedStops(bare, 1)[1]);
        everySpanIsPositive(over_drone);
        everySpanIsPositive(bare);
    }

    SECTION("a harmonic chord struck over ringing opens breaks the span and founds none")
    {
        // The sighted figure: an open texture accumulates, then natural harmonics are struck at
        // node 12 on strings still ringing OPEN. A node on a string audibly sounding the open
        // string DISPLACES it — Law A reads the sound, not the grip — so the standing span breaks
        // at the chord, never printing the nodes from a front before which those strings rang
        // open. The chord itself founds nothing, though the sixth string's open ring still sounds
        // under it: a natural states no grip, so the slot states no fretting-hand stop and founding
        // is never asked there (shape rules 1 and 7).
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 4, 0, Fraction{2}),
            noteAt(1, Fraction{1, 2}, 5, 0, Fraction{15, 8}),
            noteAt(2, Fraction{}, 6, 0, Fraction{7, 4}),
            harmonicAt(3, Fraction{}, 4, 12.0, Fraction{1, 2}),
            harmonicAt(3, Fraction{}, 5, 12.0, Fraction{1, 2}),
        }));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 2}});
        noPostureHoldsANaturalNode(derived);
        everySpanIsPositive(derived);
    }

    SECTION("a landing carries a struck open drone into the landed grip")
    {
        // One string slides over a struck open drone. The pair is a statement and its span stands
        // to the landing; the drone was struck in that span and rings on past it, so it survives
        // into the landed chord exactly as a held 7 does. Nothing sounds INSIDE the successor, so
        // it carries no mark: the shape was slid into, and a bracket would show nothing new.
        const auto slide_over = [](const int drone_fret) {
            return deriveFrom(streamOf({
                travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{1}), {{Fraction{1, 2}, 7}}),
                noteAt(1, Fraction{}, 6, drone_fret, Fraction{1}),
            }));
        };
        const ChartShapes over_drone = slide_over(0);
        REQUIRE(over_drone.shapes.size() == 2);
        CHECK(over_drone.shapes[1].place == Fraction{1, 2});
        CHECK(over_drone.shapes[1].landing_opened);
        CHECK(derivedStops(over_drone, 1)[0] == std::optional{fretStop(7)});
        CHECK(derivedStops(over_drone, 1)[5] == std::optional{fretStop(0)});
        CHECK_FALSE(over_drone.shapes[1].bracket_place.has_value());
        CHECK_FALSE(over_drone.shapes[1].sounds_in_parts);
        everySpanIsPositive(over_drone);

        const ChartShapes over_fretted = slide_over(7);
        REQUIRE(over_fretted.shapes.size() == 2);
        CHECK(over_fretted.shapes[1].place == over_drone.shapes[1].place);
        CHECK(over_fretted.shapes[1].sustain == over_drone.shapes[1].sustain);
        everySpanIsPositive(over_fretted);
    }

    SECTION("an older ring founds nothing, at a pressed stop or a natural's chime")
    {
        // A ring struck inside a span that has since closed counts toward no later founding,
        // whatever stop it states (shape rule 1). An artificial harmonic PRESSES its fret under
        // the damped node, so it states that pressed stop and is a member of the chord that
        // struck it; a natural harmonic's ring states no grip at all, so the chord that struck it
        // grips the two fretted notes alone. Either way the two notes that follow are two
        // statements since the close, under the minimum, and the older ring beside them is no
        // third: both figures derive the chord's span alone.
        const auto figure = [](ChartNote sixth) {
            return streamOf({
                std::move(sixth),
                noteAt(1, Fraction{}, 1, 5, Fraction{1, 4}),
                noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
                noteAt(2, Fraction{}, 1, 3, Fraction{1, 2}),
                noteAt(3, Fraction{}, 2, 9, Fraction{1, 4}),
            });
        };
        const ChartShapes natural =
            deriveFrom(figure(harmonicAt(1, Fraction{}, 6, 12.0, Fraction{1})));
        REQUIRE(natural.shapes.size() == 1);
        CHECK(natural.shapes[0].closing_onset_place == std::optional{Fraction{1, 4}});
        CHECK_FALSE(derivedStops(natural, 0)[5].has_value());
        noPostureHoldsANaturalNode(natural);

        ChartNote artificial = noteAt(1, Fraction{}, 6, 3, Fraction{1});
        artificial.harmonic_node = 15.0;
        const ChartShapes pressed = deriveFrom(figure(artificial));
        REQUIRE(pressed.shapes.size() == 1);
        CHECK(pressed.shapes[0].closing_onset_place == natural.shapes[0].closing_onset_place);
        CHECK(pressed.shapes[0].sustain == natural.shapes[0].sustain);
        CHECK(derivedStops(pressed, 0)[5] == std::optional{fretStop(3)});
        noPostureHoldsANaturalNode(pressed);
        everySpanIsPositive(natural);
        everySpanIsPositive(pressed);
    }
}

// THE FRETTED HABITAT of the same law. Pedal tones and let-ring arpeggios put FRETTED rings under
// a moving line exactly as drones do. A ring struck behind the coverage frontier founds nothing
// (shape rule 1), so a line note that breaks the grip heads no bracket over the rings still
// standing on its own; where the line's own onsets DO found a span, those rings join it as carried
// members, so the bracket lists them beside the notes that founded it.
TEST_CASE(
    "Fretted rings under a moving line found nothing on their own, and join a span that does",
    "[core][chart]")
{
    SECTION("two fretted pedals under a melody bracket only the melody note they founded with")
    {
        // Two let-ring pedals on the bass strings, then a melody moving above them. The pedals and
        // the first melody note accumulate into one shape, dated at the earlier pedal; the melody's
        // next note contradicts its own string and closes it. That note founds nothing over the
        // two pedals still sounding, which were struck behind the close, and neither does the
        // note after it: a pedal under a melody brackets nothing.
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 6, 3, Fraction{2}),
            noteAt(1, Fraction{1, 2}, 5, 5, Fraction{15, 8}),
            noteAt(2, Fraction{}, 1, 7, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 8, Fraction{1, 4}),
            noteAt(4, Fraction{}, 1, 10, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{1, 2}});
        CHECK(memberCount(derived, 0) == 3);
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 0);
        CHECK(grip[5] == std::optional{fretStop(3)});
        CHECK(grip[4] == std::optional{fretStop(5)});
        CHECK(grip[0] == std::optional{fretStop(7)});
        everySpanIsPositive(derived);
    }

    SECTION("an arpeggio picked on through a voice move founds anew, fronted at the moved note")
    {
        // A let-ring arpeggio whose top voice moves: the moved note closes the first shape, but
        // founds nothing at once over the two lower voices still ringing, which were struck behind
        // that close (shape rule 1). Their re-picks are statements made since it, and the third
        // of the three founds the successor. Each re-pick restates a stop whose ring reached it
        // with the moved note sounding beside it, so it inherits its string's earlier beginning
        // (re-articulation), floored at the close: the successor fronts at the MOVED NOTE, and
        // its grip is the three stops sounding there.
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{3, 4}),
            noteAt(1, Fraction{1, 2}, 2, 7, Fraction{1, 2}),
            noteAt(2, Fraction{}, 1, 5, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 6, Fraction{1, 2}),
            noteAt(3, Fraction{1, 2}, 2, 7, Fraction{3, 8}),
            noteAt(4, Fraction{}, 3, 9, Fraction{1, 4}),
        }));

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        CHECK(memberCount(derived, 1) == 3);
        const std::vector<std::optional<ChartStop>>& refounded = derivedStops(derived, 1);
        CHECK(refounded[0] == std::optional{fretStop(6)});
        CHECK(refounded[1] == std::optional{fretStop(7)});
        CHECK(refounded[2] == std::optional{fretStop(9)});
        everySpanIsPositive(derived);
    }

    SECTION("a chord struck over an older fretted ring lists it, and sounds in parts")
    {
        // The membership arm asked of a chord: a fretted note struck in an earlier span rings on
        // under a chord struck later. The chord founds on its own stroke and dates from it; the
        // older ring still sounding there joins the grip as a carried member (shape rule 1), one
        // the chord did not strike, so the span sounds in parts (shape rule 5). It dies with the
        // chord's own rings, so the span closes whole.
        const ChartShapes derived = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 4, 7, Fraction{1}),
            noteAt(1, Fraction{}, 5, 5, Fraction{1, 4}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 2}),
        }));

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        CHECK(memberCount(derived, 1) == 3);
        CHECK(derivedStops(derived, 1)[3] == std::optional{fretStop(7)});
        CHECK(derived.shapes[1].sounds_in_parts);
        everySpanIsPositive(derived);
    }
}

// Shape rule 1: a ring struck before a span's front and still sounding there joins its grip as a
// carried member, fretted or open, though it counts toward no founding: the span is founded and
// dated on its own onsets.
TEST_CASE("A ring sounding into a founding joins its grip", "[core][chart]")
{
    SECTION("the One Last Breath bass G: a held bass joins the picked figure founded over it")
    {
        // String 1 fret 3 struck on the downbeat rings a whole note; the open strings and string 5
        // fret 3 picked from beat 3 on found an accumulation on their own three statements, dated
        // at the first of them. The bass still sounding there joins as a carried member and bounds
        // the span; it dies with the picks, so the span closes there whole.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 3, Fraction{1}),
            noteAt(1, Fraction{}, 2, 2, Fraction{1, 8}),
            noteAt(1, Fraction{1, 2}, 3, 0, Fraction{1, 8}),
            noteAt(3, Fraction{}, 3, 0, Fraction{1, 2}),
            noteAt(3, Fraction{1, 2}, 4, 0, Fraction{3, 8}),
            noteAt(4, Fraction{}, 5, 3, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].sounds_in_parts);
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 1);
        CHECK(grip[0] == std::optional{fretStop(3)});
        CHECK(grip[2] == std::optional{fretStop(0)});
        CHECK(grip[3] == std::optional{fretStop(0)});
        CHECK(grip[4] == std::optional{fretStop(3)});
    }

    SECTION("the Example bar 2 figure: the rings still sounding from the last figure join the next")
    {
        // A picked figure through bar 1 ends on string 5 fret 1 and the open string 6, ringing
        // into bar 2; the bar-2 figure (string 4 fret 3, string 3 fret 2, string 2 fret 3) founds
        // on its own three statements, and the two rings join it as carried members, completing
        // its grip and bounding it where they stop ringing, with the figure's own rings.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(2, Fraction{}, 2, 3, Fraction{1}),
            noteAt(2, Fraction{1, 2}, 3, 2, Fraction{3, 4}),
            noteAt(3, Fraction{}, 4, 0, Fraction{1, 2}),
            noteAt(3, Fraction{1, 2}, 5, 1, Fraction{1, 4}),
            noteAt(4, Fraction{}, 6, 0, Fraction{3, 4}),
            noteAt(4, Fraction{1, 2}, 5, 1, Fraction{5, 8}),
            inMeasure(2, noteAt(1, Fraction{}, 4, 3, Fraction{1, 2})),
            inMeasure(2, noteAt(1, Fraction{1, 2}, 3, 2, Fraction{3, 8})),
            inMeasure(2, noteAt(2, Fraction{}, 2, 3, Fraction{1, 4})),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 1);
        CHECK(grip[1] == std::optional{fretStop(3)});
        CHECK(grip[2] == std::optional{fretStop(2)});
        CHECK(grip[3] == std::optional{fretStop(3)});
        CHECK(grip[4] == std::optional{fretStop(1)});
        // The open string 6, struck at 1:4 and ringing to 2:3, joins as well.
        CHECK(grip[5] == std::optional{fretStop(0)});
    }

    SECTION("a fretted ring under a lone note founds nothing")
    {
        // The ring struck in the closed span counts toward no founding, and the lone note is one
        // statement since the close, under the accumulation minimum, so sound alone founds
        // nothing.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 1, 5, Fraction{1}),
            noteAt(1, Fraction{}, 2, 7, Fraction{1, 4}),
            noteAt(2, Fraction{}, 3, 7, Fraction{1, 4}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
    }
}

// Shape rules 1 and 7: founding is asked only where the fretting hand states a stop, and only of
// statements made since the coverage frontier, so a slot of bare taps or natural nodes over a
// standing sound opens nothing, however many rings stand under it, and neither does one lone
// fretting-hand strike; and a carried member never folds in on a string a tap sounds at the slot,
// since the tap has floored it.
TEST_CASE("Only a fretting-hand stop founds a span over standing rings", "[core][chart]")
{
    // A four-string chord whose string 4 quits an eighth in, breaking the grip while strings 1 to
    // 3 ring on; `later` is struck a quarter in, over the three standing rings.
    const auto over_rings = [](std::vector<ChartNote> later) {
        later.push_back(noteAt(1, Fraction{}, 1, 5, Fraction{1}));
        later.push_back(noteAt(1, Fraction{}, 2, 7, Fraction{1}));
        later.push_back(noteAt(1, Fraction{}, 3, 9, Fraction{1}));
        later.push_back(noteAt(1, Fraction{}, 4, 11, Fraction{1, 8}));
        return deriveFrom(streamOf(std::move(later)));
    };

    SECTION("a lone tap on a fourth string opens no span")
    {
        const ChartShapes derived = over_rings({tapAt(2, Fraction{}, 5, 12, Fraction{1, 4})});
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});

        // The control one attack apart: the same note fretted founds nothing either. It states a
        // stop, so founding is asked, but the three rings were struck behind the break and count
        // toward nothing (shape rule 1), and one statement is no accumulation.
        const ChartShapes fretted = over_rings({noteAt(2, Fraction{}, 5, 12, Fraction{1, 4})});
        REQUIRE(fretted.shapes.size() == 1);
        CHECK(fretted.shapes[0].sustain == Fraction{1, 8});
    }

    SECTION("a tap beside a fretting strike keeps its string out of the span founded")
    {
        // A lone fretting strike beside the tap founds nothing over the rings (shape rule 1).
        const ChartShapes lone = over_rings({
            tapAt(2, Fraction{}, 1, 3, Fraction{1, 4}),
            noteAt(2, Fraction{}, 5, 12, Fraction{1, 4}),
        });
        REQUIRE(lone.shapes.size() == 1);
        CHECK(lone.shapes[0].sustain == Fraction{1, 8});

        // A fretting dyad beside it does: two stops at one slot found on their own onsets, and the
        // rings still sounding there join as carried members. The tap on string 1 below its
        // ringing 5 floors that string, so the 5 is a tail there and folds in on nothing; strings
        // 2 and 3 alone are carried.
        const ChartShapes derived = over_rings({
            tapAt(2, Fraction{}, 1, 3, Fraction{1, 4}),
            noteAt(2, Fraction{}, 5, 12, Fraction{1, 4}),
            noteAt(2, Fraction{}, 6, 12, Fraction{1, 4}),
        });
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 4});
        CHECK(derived.shapes[1].sounds_in_parts);
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 1);
        CHECK_FALSE(grip[0].has_value());
        CHECK(grip[1] == std::optional{fretStop(7)});
        CHECK(grip[2] == std::optional{fretStop(9)});
        CHECK(grip[4] == std::optional{fretStop(12)});
        CHECK(grip[5] == std::optional{fretStop(12)});
        CHECK(memberCount(derived, 1) == 4);
    }

    SECTION("natural nodes struck over the rings open no span")
    {
        // A natural harmonic states no grip, so a slot of nodes states no fretting-hand stop and
        // founds nothing, exactly as the bare tap does.
        const ChartShapes chimed = over_rings({
            harmonicAt(2, Fraction{}, 5, 12.0, Fraction{1, 4}),
            harmonicAt(2, Fraction{}, 6, 12.0, Fraction{1, 4}),
        });
        REQUIRE(chimed.shapes.size() == 1);
        CHECK(chimed.shapes[0].sustain == Fraction{1, 8});
        noPostureHoldsANaturalNode(chimed);

        // The control: the same two strings fretted state a grip, which founds on its own whole
        // stroke and dates from it; the three rings still sounding there join it as carried
        // members (shape rule 1).
        const ChartShapes fretted = over_rings({
            noteAt(2, Fraction{}, 5, 12, Fraction{1, 4}),
            noteAt(2, Fraction{}, 6, 12, Fraction{1, 4}),
        });
        REQUIRE(fretted.shapes.size() == 2);
        CHECK(fretted.shapes[1].place == Fraction{1, 4});
        CHECK(memberCount(fretted, 1) == 5);
        CHECK(fretted.shapes[1].sounds_in_parts);
    }
}

// Shape rule 1, on the sighted figure: a statement after a break founds on its own onsets, with the
// rings still standing at its front carried into its grip; a carried ring dying before any later
// slot SPLITS that span where it dies, and the span runs on from the death with the members still
// standing, so the strike after the death is judged against that remainder.
TEST_CASE("A carried ring's death splits the span a strike after a break founds", "[core][chart]")
{
    // 1:1 to 1:3 accumulate string 3's 9, string 2's 7 and string 1's 5; at 1:4 string 2 moves to
    // 8 with string 4 struck beside it, while string 3's 9 rings on to 2:1; at 2:1.5 string 1 is
    // struck again at 3 over the 8 and the 10.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 3, 9, Fraction{1}),
        noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
        noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
        noteAt(4, Fraction{}, 2, 8, Fraction{1, 2}),
        noteAt(4, Fraction{}, 4, 10, Fraction{1, 2}),
        inMeasure(2, noteAt(1, Fraction{1, 2}, 1, 3, Fraction{1, 8})),
    });
    const ChartShapes derived = deriveFrom(notes);
    REQUIRE(derived.shapes.size() == 3);

    // The accumulation, founded at 1:3 and dated at 1:1, closed by the moved finger at 1:4.
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sustain == Fraction{3, 4});
    CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{3, 4}});
    const std::vector<std::optional<ChartStop>>& first = derivedStops(derived, 0);
    CHECK(first[0] == std::optional{fretStop(5)});
    CHECK(first[1] == std::optional{fretStop(7)});
    CHECK(first[2] == std::optional{fretStop(9)});
    CHECK_FALSE(first[3].has_value());

    // The dyad at the frontier founds there on its own two stops, with string 3's 9 carried into
    // its grip, so it sounds in parts; the 9 dies at 2:1 before any later slot, and as a carried
    // member its death splits the span there rather than closing it.
    CHECK(derived.shapes[1].place == Fraction{3, 4});
    CHECK(derived.shapes[1].sustain == Fraction{1, 4});
    CHECK_FALSE(derived.shapes[1].closing_onset_place.has_value());
    CHECK(derived.shapes[1].sounds_in_parts);
    const std::vector<std::optional<ChartStop>>& second = derivedStops(derived, 1);
    CHECK_FALSE(second[0].has_value());
    CHECK(second[1] == std::optional{fretStop(8)});
    CHECK(second[2] == std::optional{fretStop(9)});
    CHECK(second[3] == std::optional{fretStop(10)});

    // The remainder runs on from the death with the 8 and the 10, and the next strike, on a string
    // it never stated, grows it: growth by a string never stated sounds in parts (shape rule 5).
    CHECK(derived.shapes[2].place == Fraction{1});
    CHECK(derived.shapes[2].sustain == Fraction{1, 4});
    CHECK(derived.shapes[2].sounds_in_parts);
    const std::vector<std::optional<ChartStop>>& third = derivedStops(derived, 2);
    CHECK(third[0] == std::optional{fretStop(3)});
    CHECK(third[1] == std::optional{fretStop(8)});
    CHECK_FALSE(third[2].has_value());
    CHECK(third[3] == std::optional{fretStop(10)});
    everySpanIsPositive(derived);
}

// Shape rules 1 and 5 on the figures the user sighted. A ring still sounding where a span founds
// joins its grip as a carried member, open or fretted, and a member the founding stroke did not
// strike makes the span sound in parts: an arpeggio from three members up. The carried member's
// death splits the span, and the remainder is classed on the hand's own strokes alone.
TEST_CASE("A carried member makes its span sound in parts", "[core][chart]")
{
    SECTION("Stay Together bar 17: a dyad over a ringing open string is one arpeggio")
    {
        // The open string 2 rings in from the last figure; string 3 fret 7 and string 4 fret 9
        // strike over it, and the open string is re-picked half a beat later. One span from the
        // strike, holding 0 7 9, in parts. The re-pick restates the carried 0 at its own stop,
        // which makes it the span's own (shape rule 1), so nothing splits there.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(1, Fraction{}, 2, 0, Fraction{1, 2}),
            noteAt(1, Fraction{}, 3, 2, Fraction{1, 4}),
            noteAt(1, Fraction{}, 4, 4, Fraction{1, 4}),
            noteAt(2, Fraction{}, 3, 7, Fraction{1, 2}),
            noteAt(2, Fraction{}, 4, 9, Fraction{1, 2}),
            noteAt(2, Fraction{1, 2}, 2, 0, Fraction{3, 8}),
        });
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 4});
        const std::vector<std::optional<ChartStop>>& grip = derivedStops(derived, 1);
        CHECK(grip[1] == std::optional{fretStop(0)});
        CHECK(grip[2] == std::optional{fretStop(7)});
        CHECK(grip[3] == std::optional{fretStop(9)});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(arpeggiosFrom(notes)[1]);
        // THE BOX LAW is display and unconditional: the 7/9 strike is two notes struck together,
        // so it wears its own chord box inside the arpeggio span.
        const HighwayChordGrouping boxes = boxesFrom(notes);
        const std::size_t strike_group = boxes.note_group[indexAt(notes, 1, 2, 3)];
        REQUIRE(strike_group < boxes.groups.size());
        CHECK(boxes.groups[strike_group].fretting_hand_count == 2);
        CHECK(boxes.groups[strike_group].box_treatment == HighwayChordBoxTreatment::Full);
    }

    SECTION("the sighting bars: a carried member makes the chord an arpeggio; a strum is a box")
    {
        // Bar 1: string 3 fret 7 and string 4 fret 9 strike together (the 9 a 32nd, so the pair's
        // span closes and the 7 rings on from behind the frontier); the chord string 1 fret 3 and
        // string 2 fret 5 strikes on beat 3 while the 7 rings, carrying it in: an arpeggio until
        // the 7 dies, then the chord's own box. Bar 3: all three strings struck.
        const std::vector<ChartNote> carried = streamOf({
            noteAt(1, Fraction{}, 3, 7, Fraction{9, 16}),
            noteAt(1, Fraction{}, 4, 9, Fraction{1, 32}),
            noteAt(3, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(3, Fraction{}, 2, 5, Fraction{1, 2}),
        });
        const ChartShapes derived = deriveFrom(carried);
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[0].sustain == Fraction{1, 32});
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 16});
        CHECK(derivedStops(derived, 1)[2] == std::optional{fretStop(7)});
        CHECK(derived.shapes[2].place == Fraction{9, 16});
        CHECK(derived.shapes[2].sustain == Fraction{7, 16});
        CHECK(memberCount(derived, 2) == 2);
        CHECK(arpeggiosFrom(carried) == std::vector<bool>{false, true, false});

        const std::vector<ChartNote> struck = streamOf({
            noteAt(1, Fraction{}, 1, 3, Fraction{1, 2}),
            noteAt(1, Fraction{}, 2, 5, Fraction{1, 2}),
            noteAt(1, Fraction{}, 3, 5, Fraction{1, 8}),
        });
        REQUIRE(deriveFrom(struck).shapes.size() == 1);
        CHECK(arpeggiosFrom(struck) == std::vector<bool>{false});
    }
}

// The chord->parts direction (shape rule 5) reads the class that prints: a span a carried ring
// makes sound in parts already wears the bracket that covers a partial sounding, so a restrike of
// part of its own grip rides it, where the same restrike over a box closes the box.
TEST_CASE(
    "A partial restrike continues a span a carried ring makes sound in parts", "[core][chart]")
{
    // The open string 1 is struck on 1:1 beside string 2's 2, whose eighth closes their span, so
    // `open_ring` sounds on behind the frontier. String 3's 4 and string 4's 4 strike together on
    // 1:3, string 4 ringing to 2:3; string 3 alone is restruck on 2:1.
    const auto figure = [](const Fraction open_ring) {
        return streamOf({
            noteAt(1, Fraction{}, 1, 0, open_ring),
            noteAt(1, Fraction{}, 2, 2, Fraction{1, 8}),
            noteAt(3, Fraction{}, 3, 4, Fraction{1, 2}),
            noteAt(3, Fraction{}, 4, 4, Fraction{1}),
            inMeasure(2, noteAt(1, Fraction{}, 3, 4, Fraction{1, 2})),
        });
    };

    SECTION("the open string carried into the dyad's span makes the restrike ride it")
    {
        // The open string rings to 2:3 and joins the dyad's span as a carried member, so the span
        // sounds in parts from its front, and string 3 restruck alone runs it on to the end.
        const std::vector<ChartNote> notes = figure(Fraction{3, 2});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK_FALSE(derived.shapes[1].closing_onset_place.has_value());
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(derivedStops(derived, 1)[0] == std::optional{fretStop(0)});
        CHECK(arpeggiosFrom(notes) == std::vector<bool>{false, true});
        everySpanIsPositive(derived);
    }

    SECTION("with nothing carried, the same restrike closes the box")
    {
        // The open string dies with string 2's eighth, so the dyad's span is a box, and string 3
        // restruck without string 4 is the statement coming apart: the box closes at the
        // restrike, and the lone restrike over string 4's ring founds nothing.
        const std::vector<ChartNote> notes = figure(Fraction{1, 8});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].closing_onset_place == std::optional{Fraction{1}});
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
        CHECK(arpeggiosFrom(notes) == std::vector<bool>{false, false});
        everySpanIsPositive(derived);
    }
}

// THE SPLIT (shape rule 1): a carried member bounds its span, but the span exists on its own
// onsets, so the carried member's death splits it rather than closing it. The piece up to the
// death is published with the carried member and sounds in parts; the remainder runs on from the
// death with the members still standing, classed on the hand's own strokes alone (shape rule 5).
TEST_CASE("A carried member's death splits its span", "[core][chart]")
{
    // THE SIGHTING FIGURE, a measure in: string 3's 9 on 2:1, string 2's 7 on 2:2 and string 1's 5
    // on 2:3 accumulate, founding at 2:3 and dating from 2:1. Beneath them an OLDER open string 4
    // struck on 1:3 in a dyad whose own span closed an eighth later, so it lies behind the
    // frontier; `older_ring` is how long it rings from 1:3. A lone fretted note on string 6 on
    // 2:4.5 sounds over the 9 and the 7 after the figure has closed.
    const auto sighting = [](const Fraction older_ring) {
        return streamOf({
            noteAt(3, Fraction{}, 4, 0, older_ring),
            noteAt(3, Fraction{}, 5, 2, Fraction{1, 8}),
            inMeasure(2, noteAt(1, Fraction{}, 3, 9, Fraction{1})),
            inMeasure(2, noteAt(2, Fraction{}, 2, 7, Fraction{3, 4})),
            inMeasure(2, noteAt(3, Fraction{}, 1, 5, Fraction{1, 4})),
            inMeasure(2, noteAt(4, Fraction{1, 2}, 6, 3, Fraction{1, 16})),
        });
    };

    SECTION(
        "THE SIGHTING FIGURE: an older ring is carried, and its death before the founding "
        "leaves the remainder as the span")
    {
        // The older open string rings to 2:2.5: it founds nothing, but sounds at the open on 2:1
        // and outlives it, so it is carried in. It dies at 2:2.5, before the third own member has
        // sounded, so the piece up to its death is no span and is dropped; the remainder opens
        // there with the 9 and the 7 still standing, the 5 on 2:3 founds it, and the bracket runs
        // from the death. The lone note after the close founds nothing over the 9 and the 7
        // still ringing: a pedal under a melody brackets nothing.
        const ChartShapes derived = deriveFrom(sighting(Fraction{7, 8}));
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{1, 2});
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        CHECK(derived.shapes[1].place == Fraction{11, 8});
        CHECK(derived.shapes[1].sustain == Fraction{3, 8});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(memberCount(derived, 1) == 3);
        CHECK_FALSE(derivedStops(derived, 1)[3].has_value());
        CHECK(arpeggiosFrom(sighting(Fraction{7, 8})) == std::vector<bool>{false, true});
        everySpanIsPositive(derived);
    }

    SECTION("a ring ending within the quantum past the open never joins, and moves nothing")
    {
        // The older ring ending a sixty-fourth past the open, short of the fortieth of a whole
        // note the minimum sustain distance measures there: it draws nothing into the span and
        // reads as released before it, so the figure is one span on its own three members from the
        // 9's strike, the sliver a tail under its opening.
        const ChartShapes sliver = deriveFrom(sighting(Fraction{33, 64}));
        REQUIRE(sliver.shapes.size() == 2);
        CHECK(sliver.shapes[1].place == Fraction{1});
        CHECK(sliver.shapes[1].sustain == Fraction{3, 4});
        CHECK(memberCount(sliver, 1) == 3);
        CHECK_FALSE(derivedStops(sliver, 1)[3].has_value());

        // The control a thirty-second past the open, beyond the quantum: it joins, and its death
        // splits the unfounded span, whose remainder is what the 5 founds.
        const ChartShapes joined = deriveFrom(sighting(Fraction{17, 32}));
        REQUIRE(joined.shapes.size() == 2);
        CHECK(joined.shapes[1].place == Fraction{33, 32});
        CHECK(joined.shapes[1].sustain == Fraction{23, 32});
        CHECK(memberCount(joined, 1) == 3);
        CHECK_FALSE(derivedStops(joined, 1)[3].has_value());
        everySpanIsPositive(sliver);
        everySpanIsPositive(joined);
    }

    // A three-string chord strummed whole on 2:1 and ringing to 4:1, over an older fretted note on
    // string 4 struck with a dyad on 1:1 whose span closed an eighth later; the older note dies on
    // 3:1. `after` follows.
    const auto chord_over_older = [](std::vector<ChartNote> after) {
        after.push_back(noteAt(1, Fraction{}, 4, 9, Fraction{2}));
        after.push_back(noteAt(1, Fraction{}, 5, 7, Fraction{1, 8}));
        after.push_back(inMeasure(2, noteAt(1, Fraction{}, 1, 5, Fraction{2})));
        after.push_back(inMeasure(2, noteAt(1, Fraction{}, 2, 7, Fraction{2})));
        after.push_back(inMeasure(2, noteAt(1, Fraction{}, 3, 7, Fraction{2})));
        return streamOf(std::move(after));
    };

    SECTION("the remainder keeps the hand's own class: a chord struck whole is a box after it")
    {
        // The piece holding the carried 9 is an arpeggio, a member the stroke did not strike; the
        // remainder is the chord alone, which its own stroke sounded whole, so it is a box — a
        // bracket ending where the carried ring died, the chord's own tails ringing past it.
        const std::vector<ChartNote> notes = chord_over_older({});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 3);
        CHECK(derived.shapes[1].place == Fraction{1});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(memberCount(derived, 1) == 4);
        CHECK(derived.shapes[2].place == Fraction{2});
        CHECK(derived.shapes[2].sustain == Fraction{1});
        CHECK_FALSE(derived.shapes[2].sounds_in_parts);
        CHECK(memberCount(derived, 2) == 3);
        CHECK(arpeggiosFrom(notes) == std::vector<bool>{false, true, false});
        everySpanIsPositive(derived);
    }

    SECTION("a strike displacing the carried member at its death leaves no zero-length span")
    {
        // String 4 struck at 11 exactly where its carried 9 dies: the span splits there, the
        // piece holding the 9 published to its death. The strike displaces the 9 and so breaks the
        // remainder at its own front, a span closing where it opens, which publishes nothing; and
        // the 11 is one statement over rings struck behind the frontier, so it founds nothing.
        // Every span the figure derives has length.
        const std::vector<ChartNote> notes =
            chord_over_older({inMeasure(3, noteAt(1, Fraction{}, 4, 11, Fraction{1}))});
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(derivedStops(derived, 1)[3] == std::optional{fretStop(9)});
        // The displacing strike stands at the close, so the piece keeps its distance from it.
        CHECK(derived.shapes[1].closing_onset_place == std::optional{Fraction{2}});
        everySpanIsPositive(derived);
    }

    SECTION("a note struck inside the accumulation and dying before it founds breaks it")
    {
        // String 3's 9 on 1:1, string 2's 7 on 1:2 and string 1's 5 on 1:3 accumulate into one
        // span from 1:1. A lone string 4 at 3, struck on 1:1.5 and dead by 1:2, grows the span the
        // 9 opened and then quits, which breaks it before it is founded; the 7 opens anew with the
        // 9 carried, and the 7 and the 5 are two own statements, under the thresholds (shape rule
        // 1). The figure derives nothing.
        const ChartShapes plain = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
        }));
        REQUIRE(plain.shapes.size() == 1);
        CHECK(plain.shapes[0].place == Fraction{});

        const ChartShapes broken = deriveFrom(streamOf({
            noteAt(1, Fraction{}, 3, 9, Fraction{1}),
            noteAt(1, Fraction{1, 2}, 4, 3, Fraction{1, 8}),
            noteAt(2, Fraction{}, 2, 7, Fraction{3, 4}),
            noteAt(3, Fraction{}, 1, 5, Fraction{1, 4}),
        }));
        CHECK(broken.shapes.empty());
    }
}

// A same-stop restrike of a member of a standing grip renews it (shape rule 3), so a figure whose
// first statements are restruck runs from where they were first made. The figure's timing: a
// closer dying exactly into a pull-off, and a slot a quarter later that restrikes the stop the
// pull-off left down.
TEST_CASE(
    "A restruck stop renews its span, which runs from where its statement began", "[core][chart]")
{
    const auto figure = [](const Fraction pull_off_ring, const std::optional<int> restrike_fret) {
        std::vector<ChartNote> notes{
            noteAt(3, Fraction{}, 4, 7, Fraction{1, 8}),
            pullOffAt(3, Fraction{1, 2}, 4, 5, pull_off_ring),
            noteAt(4, Fraction{}, 5, 5, Fraction{1, 2}),
            noteAt(4, Fraction{1, 2}, 6, 5, Fraction{1, 2}),
        };
        if (restrike_fret.has_value())
        {
            notes.push_back(noteAt(4, Fraction{1, 2}, 4, *restrike_fret, Fraction{1, 2}));
        }
        return streamOf(std::move(notes));
    };

    SECTION("the restrike inherits its predecessor's beginning")
    {
        // The sighting itself. The pull-off's ring abuts the restrike, the restrike names the very
        // stop the channel states there, and string five is sounding at the restrike
        // (re-articulation), so the two notes are one statement of fret 5 and the span fronts where
        // that statement began. Without the column the front would be string five's onset an eighth
        // later, because the restrike's own onset is the only date a loop can read for string
        // four.
        //
        // THE FOLD carries the beginning NO FURTHER BACK than the release here. The fret-7 note is
        // the pull-off's SOURCE, but string four was not at 5 when it spoke — a pull-off proves
        // its landing stop at the release and at no earlier instant — so the source states the 7
        // it sounds and the statement of 5 begins where the release does. The extra hop back to a
        // source exists only where the source rode above a stop already held (the surviving-hop
        // section in "A legato source above the gripped stop never seams"). The close does not
        // move.
        const ChartShapes derived = deriveFrom(figure(Fraction{1, 4}, 5));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{5, 8});
        CHECK(derived.shapes[0].sustain == Fraction{5, 8});
        everySpanIsPositive(derived);
    }

    SECTION("a carry of the same statement dates it identically")
    {
        // The twin, and the point is that BOTH PATHS GIVE ONE ANSWER: with no restrike at all the
        // string joins as a plain carry, and the span fronts at the same instant. The ring runs an
        // eighth longer because membership reads STRICTLY — a ring ending at the slot crosses
        // no slot — which is the only difference the twin can have.
        const ChartShapes derived = deriveFrom(figure(Fraction{3, 8}, std::nullopt));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{5, 8});
        CHECK(derived.shapes[0].sustain == Fraction{3, 8});
        everySpanIsPositive(derived);
    }

    SECTION("a ring that dies before the restrike hands nothing on")
    {
        // The control re-articulation needs: the pull-off's ring is short of the restrike, so no
        // predecessor reaches it to inherit from and the restrike begins its own statement. The
        // front falls back to the earliest other member — string five's onset — which is exactly
        // the answer a missing column gives for the first section. Nothing here but the ring
        // differs.
        const ChartShapes derived = deriveFrom(figure(Fraction{1, 8}, 5));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{3, 4});
        CHECK(derived.shapes[0].sustain == Fraction{1, 2});
        everySpanIsPositive(derived);
    }

    SECTION(
        "a restrike at another fret breaks the span, and the string sounding beside it is "
        "carried into the chord it opens")
    {
        // The 7 displaces the 5 still ringing on string four, which breaks the span the pull-off
        // and string five were accumulating. Struck together with string six it opens a chord
        // there, into which string five's 5 is carried: it prints, and its death splits the chord,
        // whose remainder — two stops the hand struck whole — runs on as a box.
        const ChartShapes derived = deriveFrom(figure(Fraction{1, 4}, 7));

        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{7, 8});
        CHECK(derived.shapes[0].sustain == Fraction{3, 8});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(derivedStops(derived, 0)[3] == std::optional{fretStop(7)});
        CHECK(derivedStops(derived, 0)[4] == std::optional{fretStop(5)});
        CHECK(derived.shapes[1].place == Fraction{5, 4});
        CHECK(derived.shapes[1].sustain == Fraction{1, 8});
        CHECK_FALSE(derived.shapes[1].sounds_in_parts);
        CHECK(memberCount(derived, 1) == 2);
        everySpanIsPositive(derived);
    }
}

// RE-ARTICULATION (shape rule 1): a restrike continues its string's earlier statement for dating
// only where another member of the figure is SOUNDING at the restrike. A stab whose ring abuts its
// own restrike, with nothing else sounding there, is a gesture of its own: the restrike is the
// figure's first articulation, and the arpeggio that grows from it fronts at the restrike. A tail
// is `[start, end)`, so the stab abutting the restrike sounds nothing there itself.
TEST_CASE(
    "A restrike with nothing else sounding is the figure's first articulation", "[core][chart]")
{
    // The stab on 1:1, its restrike on 1:2 and the arpeggio growing from it; `drone` is how long an
    // open string 6 struck on 1:1.5, between the stab and its restrike, rings, if it is struck.
    const auto figure = [](const std::optional<Fraction> drone) {
        std::vector<ChartNote> notes{
            noteAt(1, Fraction{}, 3, 14, Fraction{1, 4}),
            noteAt(2, Fraction{}, 3, 14, Fraction{3, 4}),
            noteAt(3, Fraction{}, 4, 16, Fraction{1, 2}),
            noteAt(4, Fraction{}, 5, 13, Fraction{1, 4}),
        };
        if (drone.has_value())
        {
            notes.push_back(noteAt(1, Fraction{1, 2}, 6, 0, *drone));
        }
        return streamOf(std::move(notes));
    };

    SECTION("the stab abutting its restrike stands alone")
    {
        const ChartShapes derived = deriveFrom(figure(std::nullopt));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 4});
        CHECK(derived.shapes[0].sustain == Fraction{3, 4});
        CHECK(memberCount(derived, 0) == 3);
        everySpanIsPositive(derived);
    }

    SECTION("a drone ringing through the figure makes the stab the figure's first note")
    {
        // The drone sounds across the restrike and on to the arpeggio's end, so it is a member
        // sounding at the restrike: the restrike continues the stab's statement, and the span
        // fronts at the stab, earlier than the drone's own onset.
        const ChartShapes derived = deriveFrom(figure(Fraction{7, 8}));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK(memberCount(derived, 0) == 4);
        everySpanIsPositive(derived);
    }

    SECTION("a drone dying before the figure founds breaks it, and the restart has too few")
    {
        // The drone grows the stab's span and still sounds across the restrike, which renews the
        // two-stop grip; then it quits, which breaks the span before a third own member has
        // sounded. The 16 opens anew with the 14 carried, and the 16 and the 13 are two own
        // statements, under the thresholds (shape rule 1): the figure derives nothing.
        CHECK(deriveFrom(figure(Fraction{1, 4})).shapes.empty());
    }
}

// Renewal is a grip's (shape rule 3): a span stating one stop of its own beside a carried ring has
// no grip to renew, so that stop's restrike opens the figure anew, and the chord struck there
// brackets from itself rather than from the lone note before it.
TEST_CASE(
    "A lone note before a chord is its own gesture whatever rings beneath it", "[core][chart]")
{
    // The open string 1 is struck on 1:1 beside string 2's 2, whose eighth closes their span, so it
    // drones on behind the frontier to 3:1. String 3's 4 sounds on 2:1, ringing exactly to 2:3,
    // where it is restruck together with string 5's 2, both ringing to 3:1. `beside` is struck with
    // string 3 on 2:1.
    const auto figure = [](std::vector<ChartNote> beside) {
        beside.push_back(noteAt(1, Fraction{}, 1, 0, Fraction{2}));
        beside.push_back(noteAt(1, Fraction{}, 2, 2, Fraction{1, 8}));
        beside.push_back(inMeasure(2, noteAt(1, Fraction{}, 3, 4, Fraction{1, 2})));
        beside.push_back(inMeasure(2, noteAt(3, Fraction{}, 3, 4, Fraction{1, 2})));
        beside.push_back(inMeasure(2, noteAt(3, Fraction{}, 5, 2, Fraction{1, 2})));
        return streamOf(std::move(beside));
    };

    SECTION("the lone note's restrike opens the figure, and the chord brackets from itself")
    {
        // The lone note opens a span over the carried drone holding one stop of its own, which
        // renews nothing at its restrike: it ends unfounded and is dropped, and the chord opens
        // its own span on 2:3 with the drone carried. Nothing is published over 2:1 to 2:3.
        const ChartShapes derived = deriveFrom(figure({}));
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
        CHECK(derived.shapes[1].place == Fraction{3, 2});
        CHECK(derived.shapes[1].bracket_place == std::optional{Fraction{3, 2}});
        CHECK(derived.shapes[1].sustain == Fraction{1, 2});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(memberCount(derived, 1) == 3);
        everySpanIsPositive(derived);
    }

    SECTION("a grip of two own stops renews at the restrike, and the span runs from the grip")
    {
        // String 4's 4 struck with string 3 on 2:1 makes that stroke a grip of two own stops. It
        // rings on through the restrike, so the restrike restates part of the grip rather than
        // all of it and more: the grip renews, string 5 grows it, and one span runs from 2:1.
        const ChartShapes derived =
            deriveFrom(figure({inMeasure(2, noteAt(1, Fraction{}, 4, 4, Fraction{1}))}));
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1});
        CHECK(derived.shapes[1].bracket_place == std::optional{Fraction{1}});
        CHECK(derived.shapes[1].sustain == Fraction{1});
        CHECK(memberCount(derived, 1) == 4);
        everySpanIsPositive(derived);
    }
}

// The renewal test is asked of a span's own stops alone (shape rule 3): a carried member whose ring
// ends exactly at a slot that re-sounds its string is the hand striking into the span, growth that
// makes the member its own, so the span stands there however few stops of its own it holds.
TEST_CASE(
    "A carried member re-sounded at its ring end is the hand striking into the span",
    "[core][chart]")
{
    // The open strings 1 and 2 are struck on 1:1 beside string 4's 2, whose eighth closes their
    // span, so they ring on behind the frontier, string 1 for `open_ring` and string 2 to 2:3,
    // where it is restruck. String 3's 4 sounds alone on 1:3 for `lone_ring`, and `restrike` is
    // struck on 2:1.
    const auto figure =
        [](const Fraction open_ring, const Fraction lone_ring, const ChartNote& restrike) {
            return streamOf({
                noteAt(1, Fraction{}, 1, 0, open_ring),
                noteAt(1, Fraction{}, 2, 0, Fraction{3, 2}),
                noteAt(1, Fraction{}, 4, 2, Fraction{1, 8}),
                noteAt(3, Fraction{}, 3, 4, lone_ring),
                restrike,
                inMeasure(2, noteAt(3, Fraction{}, 2, 0, Fraction{1, 2})),
            });
        };

    SECTION("each carried ring restruck at its end joins the lone note's span")
    {
        // The lone note opens a span over the two carried open strings. String 1 dies exactly into
        // its restrike on 2:1 and string 2 into its own on 2:3, and each restrike makes its string
        // the span's own, so three own members sound on 2:3 and found the one span from 1:3.
        const std::vector<ChartNote> notes = figure(
            Fraction{1}, Fraction{3, 2}, inMeasure(2, noteAt(1, Fraction{}, 1, 0, Fraction{1})));
        const ChartShapes derived = deriveFrom(notes);
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[1].place == Fraction{1, 2});
        CHECK(derived.shapes[1].bracket_place == std::optional{Fraction{1, 2}});
        CHECK(derived.shapes[1].sustain == Fraction{3, 2});
        CHECK(derived.shapes[1].sounds_in_parts);
        CHECK(memberCount(derived, 1) == 3);
        CHECK(arpeggiosFrom(notes) == std::vector<bool>{false, true});
        everySpanIsPositive(derived);
    }

    SECTION("the lone own stop restruck at its end renews nothing")
    {
        // String 3 dies exactly into its own restrike on 2:1 while string 1 rings on to 3:1. A span
        // holding one stop of its own has no grip to renew, so the lone note's span ends there
        // unfounded, and the restart holds two own members over string 1 carried, under both
        // thresholds: nothing is published past the opening dyad.
        const ChartShapes derived = deriveFrom(figure(
            Fraction{2}, Fraction{1, 2}, inMeasure(2, noteAt(1, Fraction{}, 3, 4, Fraction{1}))));
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{});
        CHECK(derived.shapes[0].sustain == Fraction{1, 8});
    }
}

// RE-ARTICULATION'S LIMIT: a WHOLE-GRIP STROKE standing alone is a span boundary (the absorption
// rule), so the box it founds dates from the stroke itself and reads no statement column. The
// corpus figure is a rhythm stab: a lone palm-muted eighth on one string dying exactly into a
// chord that restates that stop among others struck whole. Read through an inheriting column,
// the stop's statement would begin at the stab, whose ring abuts the strike, and the box would
// date an eighth before the stroke that says it whole, over a slot where the posture was one
// finger. Only the box is fenced: a stab ringing under a stroke that does NOT restate its string is
// a carry, the stroke is not whole over it, and the bracket that results dates from the stab
// exactly as the drone under stabs does.
TEST_CASE("A whole stroke dates its box at itself, past a lone note it restates", "[core][chart]")
{
    const auto stab_into_chord = [](const Fraction stab_ring) {
        std::vector<ChartNote> notes{
            noteAt(3, Fraction{}, 3, 14, stab_ring),
            noteAt(3, Fraction{1, 2}, 3, 14, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 4, 16, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 5, 13, Fraction{1, 4}),
        };
        notes[0].palm_mute = true;
        return streamOf(std::move(notes));
    };
    const Fraction stroke{5, 8};

    SECTION("the stab's ring dies exactly into the stroke")
    {
        const std::vector<ChartNote> notes = stab_into_chord(Fraction{1, 8});
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == stroke);
        CHECK(memberCount(derived, 0) == 3);
        CHECK_FALSE(derived.shapes[0].sounds_in_parts);
        CHECK_FALSE(arpeggiosFrom(notes).front());
        everySpanIsPositive(derived);
    }

    SECTION("a chain of stabs dying into one another, the last into the stroke")
    {
        // The other sighted shape: two abutting sixteenths on the string, an inheriting column
        // chaining the statement back through both. The box still fronts at the stroke.
        std::vector<ChartNote> notes{
            noteAt(3, Fraction{}, 3, 14, Fraction{1, 16}),
            noteAt(3, Fraction{1, 4}, 3, 14, Fraction{1, 16}),
            noteAt(3, Fraction{1, 2}, 3, 14, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 4, 16, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 5, 13, Fraction{1, 4}),
        };
        notes[0].palm_mute = true;
        notes[1].palm_mute = true;
        const std::vector<ChartNote> stream = streamOf(std::move(notes));
        const ChartShapes derived = deriveFrom(stream);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == stroke);
        CHECK_FALSE(arpeggiosFrom(stream).front());
        everySpanIsPositive(derived);
    }

    SECTION("a stab ringing under a stroke that does not restate it is a carry")
    {
        // The stroke is not whole over a string it leaves ringing: the stab carries in, the span
        // sounds in parts, and the bracket dates from the stab as the drone under stabs does.
        std::vector<ChartNote> notes{
            noteAt(3, Fraction{}, 3, 14, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 4, 16, Fraction{1, 4}),
            noteAt(3, Fraction{1, 2}, 5, 13, Fraction{1, 4}),
        };
        notes[0].palm_mute = true;
        notes = streamOf(std::move(notes));
        const ChartShapes derived = deriveFrom(notes);

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{1, 2});
        CHECK(derived.shapes[0].sounds_in_parts);
        CHECK(arpeggiosFrom(notes).front());
        everySpanIsPositive(derived);
    }
}

// THE SAME COLUMN'S OTHER HALF: a TRAVEL begins a statement too. The glide's landing is where the
// new stop is established (shape rule 4), so a slid finger's statement begins LATER than the note
// it rides — the mirror of the restrike, which begins earlier. Both are read off one channel (\ref
// StatedStop::stated_from), so the landing needs no record of its own.
//
// The defect this pins is re-articulation's sibling: dating a lone glide under no span at its
// NOTE's onset would make the first span to fold it in front over the very beats the string spent
// on the old stop and in transit. Its coverage would freeze at that first landing for the same
// reason — nothing is standing to restart it — so the bogus front would come with a bogus reach,
// and the pair would emit a span that exists over nothing but the travel.
TEST_CASE("A lone glide's landing opens the span a later dyad founds", "[core][chart]")
{
    // String one glides 5 to 7 across the first beat and rings on; a dyad three quarters later
    // grows the span its landing opened and founds it.
    const std::vector<ChartNote> notes = streamOf({
        travellingAt(noteAt(1, Fraction{}, 1, 5, Fraction{2}), {{Fraction{1, 4}, 7}}),
        inMeasure(2, noteAt(1, Fraction{}, 5, 3, Fraction{1, 2})),
        inMeasure(2, noteAt(1, Fraction{}, 6, 3, Fraction{1, 2})),
    });
    const ChartShapes derived = deriveFrom(notes);

    // ONE span, fronted at the landing in measure 1 beat 2 and reaching the dyad's own rings: the
    // span the glide's strike opened closed at the landing unpublished, the landing opened the
    // next with the 7, and the dyad made it three.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{1, 4});
    CHECK(derived.shapes[0].sustain == Fraction{5, 4});
    REQUIRE(derived.shapes[0].posture < derived.postures.size());
    CHECK(derived.postures[derived.shapes[0].posture].stops[0] == std::optional{fretStop(7)});
    everySpanIsPositive(derived);
}

// THE FOREIGN-SOUND FLOOR (Law A generalized; both figures sighted in the editor). A displacement
// JUNCTION record — written only where a strike coexists with the ring it replaces, and reset by a
// strike over silence — is too narrow, because the law's own sentence bounds the front by what a
// stated string was AUDIBLY doing. Put a gap of silence between the foreign ring's death and the
// restating strike and no junction ever exists: the floor collapses to the coverage frontier and
// the fronted bracket prints a fret over instants the string audibly spent under another stop. The
// record is the END of the string's last foreign sound, written at every taking strike and never
// reset, so a span fronts where the foreign sound ends no matter how the grip was established.
TEST_CASE("A span cannot date across a foreign ring that died before its strike", "[core][chart]")
{
    // The sighted arpeggio, restated once so the accumulation stands long enough to backdate:
    // three strings enter at eighth-note spacing in measure 2 and the earliest entry is what a
    // junction-only floor lets the front reach.
    const std::vector<ChartNote> arpeggio = streamOf({
        inMeasure(2, noteAt(1, Fraction{}, 2, 5, Fraction{1, 2})),
        inMeasure(2, noteAt(1, Fraction{1, 2}, 3, 7, Fraction{1, 2})),
        inMeasure(2, noteAt(2, Fraction{}, 4, 0, Fraction{1, 2})),
        inMeasure(2, noteAt(3, Fraction{}, 2, 5, Fraction{1, 4})),
        inMeasure(2, noteAt(3, Fraction{1, 2}, 3, 7, Fraction{1, 4})),
        inMeasure(2, noteAt(4, Fraction{}, 4, 0, Fraction{1, 4})),
    });
    const auto with_lead_in = [&arpeggio](std::vector<ChartNote> lead_in) {
        lead_in.insert(lead_in.end(), arpeggio.begin(), arpeggio.end());
        return lead_in;
    };

    SECTION("a gap on the restated string itself")
    {
        // Measure 1's f0 rings on the span's own third string past the figure's first entry and
        // dies a sixteenth before its own f7 strike. The f0 opened a span the 5 grew; its quit
        // breaks that span, and the f7 opens the figure anew with the 5 carried. The 5's restrike
        // on beat 3 makes it the span's own, the third own member, and the span fronts at the f7.
        const ChartShapes derived = deriveFrom(with_lead_in(streamOf({
            noteAt(4, Fraction{}, 3, 0, Fraction{5, 16}),
        })));

        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].place == Fraction{9, 8});
        CHECK(derived.shapes[0].sustain == Fraction{5, 8});
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        const std::vector<std::optional<ChartStop>>& frets =
            derived.postures[derived.shapes[0].posture].stops;
        REQUIRE(frets.size() >= 4);
        CHECK(frets[1] == std::optional{fretStop(5)});
        CHECK(frets[2] == std::optional{fretStop(7)});
        CHECK(frets[3] == std::optional{fretStop(0)});
        everySpanIsPositive(derived);
    }

    SECTION("a gap on a different stated string")
    {
        // The second sighting: the foreign f5 lives on the FOURTH string — struck an eighth
        // after the f5 dies — and the lead-in dyad emits a real predecessor span closing exactly
        // at measure 2, proving the coverage frontier alone cannot hold the front either.
        const ChartShapes derived = deriveFrom(with_lead_in(streamOf({
            noteAt(4, Fraction{}, 3, 0, Fraction{1, 4}),
            noteAt(4, Fraction{}, 4, 5, Fraction{3, 8}),
        })));

        // The predecessor dyad stands untouched, and the successor fronts at the f7 strike — the
        // earliest member onset past the f5's death — never at the frontier the predecessor's close
        // left at measure 2 beat 1.
        REQUIRE(derived.shapes.size() == 2);
        CHECK(derived.shapes[0].place == Fraction{3, 4});
        CHECK(derived.shapes[0].sustain == Fraction{1, 4});
        CHECK(derived.shapes[1].place == Fraction{9, 8});
        CHECK(derived.shapes[1].sustain == Fraction{5, 8});
        everySpanIsPositive(derived);
    }
}

// The sighted figure (a power-chord riff into an arpeggiated grip): string 5's 5 rings from the
// last sixteenth of the bar before to the second beat, string 6's 5 is struck on the downbeat and
// restruck on the second beat with no gap between the two, and the grip's 4 and 3 follow on the
// third and fourth beats. The 5 that opened the span quits at the second beat and breaks it; the
// restrike there opens the figure anew, and the 4 and the 3 found it from the restrike — never from
// the 4 a beat later.
TEST_CASE(
    "A restated string whose partner quits reopens the figure at the restrike", "[core][chart]")
{
    const ChartShapes derived = deriveFrom(streamOf({
        noteAt(4, Fraction{3, 4}, 5, 5, Fraction{5, 16}),
        inMeasure(2, noteAt(1, Fraction{}, 6, 5, Fraction{1, 4})),
        inMeasure(2, noteAt(2, Fraction{}, 6, 5, Fraction{3, 4})),
        inMeasure(2, noteAt(3, Fraction{}, 5, 4, Fraction{1, 2})),
        inMeasure(2, noteAt(4, Fraction{}, 4, 3, Fraction{1, 4})),
    }));

    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{5, 4});
    CHECK(derived.shapes[0].sustain == Fraction{3, 4});
    const std::vector<std::optional<ChartStop>>& stops = derivedStops(derived, 0);
    REQUIRE(stops.size() >= 6);
    CHECK(stops[5] == std::optional{fretStop(5)});
    CHECK(stops[4] == std::optional{fretStop(4)});
    CHECK(stops[3] == std::optional{fretStop(3)});
    everySpanIsPositive(derived);
}

// A SLID STRING'S FOREIGN SOUND ENDS AT ITS LANDING (shape rule 1's floor, read off the channel):
// before the landing the string audibly sounds its departure stop, which is not the stop the grip
// states. The sighted arpeggio: open strings 3 and 5 are already in force when string 1 strikes 3
// and slides to 5, and the grip states the 5. The open neighbours' statements are clamped to the
// floor like any member's, so the span fronts at the landing — never where the open strings began,
// and never at the slide's strike, either of which prints the 5 over the 3 still sounding.
TEST_CASE(
    "A carried member's open neighbours do not date a span before a slid member's landing",
    "[core][chart]")
{
    const auto figure = [](const bool open_ring_before) {
        std::vector<ChartNote> notes{
            inMeasure(2, noteAt(1, Fraction{}, 2, 3, Fraction{1, 8})),
            inMeasure(2, noteAt(1, Fraction{}, 5, 3, Fraction{1, 8})),
            inMeasure(2, pullOffAt(1, Fraction{1, 2}, 5, 0, Fraction{1})),
            inMeasure(2, noteAt(2, Fraction{}, 4, 0, Fraction{1})),
            inMeasure(2, noteAt(2, Fraction{1, 2}, 3, 0, Fraction{1, 4})),
            inMeasure(2, noteAt(3, Fraction{}, 2, 3, Fraction{1, 8})),
            inMeasure(2, noteAt(3, Fraction{1, 2}, 3, 3, Fraction{1, 8})),
            inMeasure(2, noteAt(4, Fraction{}, 2, 2, Fraction{5, 8})),
            inMeasure(3, noteAt(1, Fraction{1, 2}, 5, 0, Fraction{5, 8})),
            inMeasure(3, noteAt(2, Fraction{}, 4, 0, Fraction{19, 8})),
            inMeasure(3, noteAt(2, Fraction{1, 2}, 3, 0, Fraction{1, 2})),
            inMeasure(
                3,
                travellingAt(noteAt(3, Fraction{}, 1, 3, Fraction{1, 2}), {{Fraction{1, 8}, 5}})),
            inMeasure(3, noteAt(4, Fraction{}, 5, 0, Fraction{5, 4})),
            inMeasure(3, noteAt(4, Fraction{1, 2}, 3, 0, Fraction{1, 8})),
        };
        if (open_ring_before)
        {
            // String 1 rings open from a short dyad of its own until the slide's strike displaces
            // it, so its recorded foreign sound ends at the strike: a floor still short of the
            // landing.
            notes.push_back(noteAt(3, Fraction{}, 1, 0, Fraction{2}));
            notes.push_back(noteAt(3, Fraction{}, 6, 0, Fraction{1, 4}));
        }
        return streamOf(std::move(notes));
    };
    // The slide strikes at measure 3 beat 3 and lands an eighth note later.
    const Fraction landing{21, 8};

    for (const bool open_ring_before : {false, true})
    {
        CAPTURE(open_ring_before);
        const ChartShapes derived = deriveFrom(figure(open_ring_before));

        REQUIRE_FALSE(derived.shapes.empty());
        const ChartShape& slid = derived.shapes.back();
        CHECK(slid.place == landing);
        CHECK(slid.sustain == Fraction{3, 8});
        const std::vector<std::optional<ChartStop>>& stops =
            derivedStops(derived, derived.shapes.size() - 1);
        REQUIRE(stops.size() >= 5);
        CHECK(stops[0] == std::optional{fretStop(5)});
        CHECK(stops[2] == std::optional{fretStop(0)});
        CHECK(stops[4] == std::optional{fretStop(0)});
        everySpanIsPositive(derived);
    }
}

// THE SAME TERM AT THE JOIN (shape rule 3's floor arm): a free string that glides and LANDS inside
// a standing span sounded its departure stop until the landing, so a strike restating the landed
// stop finds its foreign sound ending after the span's front, and breaks the span rather than
// joining it. Joined, the bracket would print the landed 5 from the box's front, over the 3 the
// string still sounded there.
//
// String 3 strikes its 3 an eighth before the chord and glides to 5, landing an eighth after it:
// mid-travel at the stroke, it is no member of the box. Struck inside the box, the glide would
// join it at its strike and the landing would split the span instead (shape rule 4), so the
// glide is struck before the stroke to reach the join with the string still free.
TEST_CASE(
    "A free string that landed inside a standing span breaks it when restruck", "[core][chart]")
{
    const ChartShapes derived = deriveFrom(streamOf({
        travellingAt(noteAt(4, Fraction{1, 2}, 3, 3, Fraction{3, 4}), {{Fraction{1, 4}, 5}}),
        inMeasure(2, noteAt(1, Fraction{}, 1, 5, Fraction{1})),
        inMeasure(2, noteAt(1, Fraction{}, 2, 7, Fraction{1})),
        inMeasure(2, noteAt(3, Fraction{1, 4}, 3, 5, Fraction{1, 4})),
    }));

    // The box, closed at the restrike. The restrike then founds nothing over the chord's rings
    // still sounding: they were struck behind that close, and one statement is no accumulation
    // (shape rule 1), so the landed 5 prints in no bracket, never back over the 3 the string
    // sounded.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{1});
    CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{25, 16}});
    const std::vector<std::optional<ChartStop>>& stops = derivedStops(derived, 0);
    REQUIRE(stops.size() >= 3);
    CHECK(stops[0] == std::optional{fretStop(5)});
    CHECK(stops[1] == std::optional{fretStop(7)});
    CHECK_FALSE(stops[2].has_value());
    everySpanIsPositive(derived);
}

// A fretting-hand onset IS the hand, so the one second stop it can hold is the one a pull-off
// PLANTS beneath it: that plant is its held stop.
TEST_CASE("A pull-off source's plant is its held stop", "[core][chart]")
{
    // The source on string 1 rings to the very onset that pulls it off onto 5 — strict adjacency,
    // which is what resolves the successor as a pull — while string 2 sounds beside it with no
    // pull-off of its own.
    ChartNote pull = noteAt(3, Fraction{}, 1, 5, Fraction{1, 4});
    pull.attack = NoteAttack::Legato;
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 1, 7, Fraction{1, 2}),
        noteAt(1, Fraction{}, 2, 3, Fraction{1}),
        pull,
    });
    const ChartResolutions resolutions = chartResolutions(notes, {}, 0, makeTempoMap());
    REQUIRE(resolutions.connections.legato[indexAt(notes, 1, 3, 1)] == LegatoMotion::Pull);

    SECTION("the source holds the stop its pull-off plants")
    {
        CHECK(resolutions.held_stops[indexAt(notes, 1, 1, 1)] == std::optional{5});
    }

    SECTION("a fretting-hand onset nothing plants under holds no second stop")
    {
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 1, 1, 2)].has_value());
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 1, 3, 1)].has_value());
    }
}

// THE DEFAULT HELD FACT: a bare tap releases onto the covering posture's fret on its string, and
// onto the open string where no span or no posture entry names it.
TEST_CASE("A bare tap's held stop defaults to the grip the covering span holds", "[core][chart]")
{
    const std::vector<ChartNote> notes = chugOverTap(7);
    const ChartResolutions resolutions = chartResolutions(notes, {}, 0, makeTempoMap());

    // The span the defaults are read out of: one shape from the chord to the restrike's end,
    // stating 5, 7 and 7 on strings 1 to 3 and nothing on string 5.
    REQUIRE(resolutions.shapes.size() == 1);
    const ChartShape& span = resolutions.shapes.front();
    CHECK(span.place == Fraction{});
    CHECK(span.sustain == Fraction{1});
    const std::vector<std::optional<ChartStop>>& frets = resolutions.postures[span.posture].stops;
    CHECK(frets[2] == std::optional{fretStop(7)});
    CHECK_FALSE(frets[4].has_value());

    SECTION("a tap under a posture that states its string releases onto that fret")
    {
        CHECK(resolutions.held_stops[indexAt(notes, 1, 2, 3)] == std::optional{7});
    }

    SECTION("a tap on a string the posture never names holds nothing: the open string")
    {
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 1, 2, 5)].has_value());
    }

    SECTION("a span-less tap holds nothing: the open string")
    {
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 3, 1, 3)].has_value());
    }

    SECTION("only a right-hand onset takes one")
    {
        // The fretting hand's own onset IS the hand, so its fret is already the stop.
        CHECK_FALSE(resolutions.held_stops[indexAt(notes, 1, 1, 1)].has_value());
    }
}

// THE PRECEDENCE: the stop a PULL-OFF plants beats the default, which only ever answers where the
// notation states nothing.
TEST_CASE("A pull-off's plant beats the tap's default", "[core][chart]")
{
    // The chug figure with string 3's restrike made a pull-off from the tap onto `fret`.
    const auto held_under_pull_to = [](const int fret) {
        std::vector<ChartNote> notes = chugOverTap(7);
        for (ChartNote& note : notes)
        {
            if (note.string == 3 &&
                note.position == GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}})
            {
                note.fret = fret;
                note.attack = NoteAttack::Legato;
            }
        }
        const ChartResolutions resolutions = chartResolutions(notes, {}, 0, makeTempoMap());
        return resolutions.held_stops[indexAt(notes, 1, 2, 3)];
    };

    CHECK(held_under_pull_to(3) == std::optional{3});
    // A pull-off onto the OPEN string states that no finger is there, so the grip's 7 does not
    // answer beneath it either.
    CHECK_FALSE(held_under_pull_to(0).has_value());
}

// LIVE-DERIVED: the default is re-derived from whatever span covers the tap NOW, so an edit that
// reflows the spans around it moves the default with them — there is no stored value to go stale.
TEST_CASE("The held default follows an edit that reflows the covering span", "[core][chart]")
{
    const auto default_under = [](const std::optional<int> grip) {
        const std::vector<ChartNote> notes = chugOverTap(grip);
        const ChartResolutions resolutions = chartResolutions(notes, {}, 0, makeTempoMap());
        return resolutions.held_stops[indexAt(notes, 1, 2, 3)];
    };

    // The grip moves and the tap's release moves with it.
    CHECK(default_under(7) == std::optional{7});
    CHECK(default_under(9) == std::optional{9});
    // With string 3 withdrawn from the grip, the posture names nothing there: the open string.
    CHECK_FALSE(default_under(std::nullopt).has_value());
}

// A tapped arpeggio: taps at 16 pull off to 11, 7 and 9, each fretted stop ringing on under the
// next tap. The taps are the picking hand's sound, so they never keep the fretted 4s of the grip
// before sounding; those died where their own rings ended, and the bracket dates from the first
// fretted onset of the new grip.
TEST_CASE("A tap sounding over a dead fretted ring does not push a span's front", "[core][chart]")
{
    const auto at = [](const int measure,
                       const int beat,
                       const Fraction offset,
                       const int string,
                       const int fret,
                       const Fraction ring,
                       const NoteAttack attack) {
        ChartNote note = inMeasure(measure, noteAt(beat, offset, string, fret, ring));
        note.attack = attack;
        if (attack == NoteAttack::Tap && ring == Fraction{1, 4})
        {
            note.keyframes.push_back(Keyframe{.offset = Fraction{1, 8}, .fret = fret + 2});
        }
        return note;
    };
    const Fraction half{1, 2};
    const NoteAttack tap = NoteAttack::Tap;
    const NoteAttack pull = NoteAttack::Legato;
    const std::vector<ChartNote> notes = streamOf({
        at(1, 1, {}, 3, 7, Fraction{5, 8}, pull),    at(1, 1, half, 2, 11, Fraction{1, 8}, tap),
        at(1, 2, {}, 2, 4, Fraction{3, 2}, pull),    at(1, 2, half, 4, 9, Fraction{1, 8}, tap),
        at(1, 3, {}, 4, 4, Fraction{5, 4}, pull),    at(1, 3, half, 3, 9, Fraction{1, 4}, tap),
        at(1, 4, half, 3, 7, Fraction{5, 8}, pull),  at(2, 3, {}, 3, 16, Fraction{1, 4}, tap),
        at(2, 4, {}, 3, 11, Fraction{5, 8}, pull),   at(2, 4, half, 2, 16, Fraction{1, 8}, tap),
        at(3, 1, {}, 2, 7, Fraction{3, 4}, pull),    at(3, 1, half, 4, 16, Fraction{1, 8}, tap),
        at(3, 2, {}, 4, 9, Fraction{3, 4}, pull),    at(3, 2, half, 3, 16, Fraction{1, 4}, tap),
        at(3, 3, half, 3, 11, Fraction{5, 8}, pull), at(3, 4, {}, 2, 16, Fraction{1, 8}, tap),
        at(3, 4, half, 2, 7, Fraction{3, 4}, pull),  at(4, 1, {}, 4, 16, Fraction{1, 8}, tap),
        at(4, 1, half, 4, 9, Fraction{3, 4}, pull),
    });
    const ChartShapes derived = deriveFrom(notes);

    const auto grip = std::ranges::find_if(derived.shapes, [&derived](const ChartShape& shape) {
        const std::vector<std::optional<ChartStop>>& stops = derived.postures[shape.posture].stops;
        return stops[1] == std::optional{fretStop(7)} && stops[2] == std::optional{fretStop(11)} &&
               stops[3] == std::optional{fretStop(9)};
    });
    REQUIRE(grip != derived.shapes.end());
    // The 11 struck at 2:4.
    CHECK(grip->place == Fraction{7, 4});
}

// A chord of 0, 5 and 4 struck at 2:1, then taps pulling off over it, the first at 11 onto 6 on a
// string whose fretted 4 died at the chord. That tap sounds over the 6 it is planted on, never over
// the dead 4, so the landing on 6 displaces nothing: it grows the chord's span, which keeps its
// front.
TEST_CASE("A tap planted on a new stop does not keep the old stop sounding", "[core][chart]")
{
    const Fraction half{1, 2};
    const std::vector<ChartNote> notes = streamOf({
        tapAt(2, half, 4, 9, Fraction{1, 8}),
        pullOffAt(3, {}, 4, 4, Fraction{1, 2}),
        inMeasure(2, noteAt(1, {}, 1, 0, Fraction{7, 2})),
        inMeasure(2, noteAt(1, {}, 2, 5, Fraction{7, 8})),
        inMeasure(2, noteAt(1, {}, 3, 4, Fraction{1, 2})),
        inMeasure(2, tapAt(2, {}, 4, 11, Fraction{1, 8})),
        inMeasure(2, pullOffAt(2, half, 4, 6, Fraction{3, 4})),
        inMeasure(2, tapAt(3, {}, 3, 11, Fraction{1, 4})),
        inMeasure(2, pullOffAt(4, {}, 3, 4, Fraction{5, 8})),
        inMeasure(2, tapAt(4, half, 2, 12, Fraction{1, 8})),
        inMeasure(3, pullOffAt(1, {}, 2, 5, Fraction{3, 4})),
    });
    const ChartShapes derived = deriveFrom(notes);

    const auto grip = std::ranges::find_if(derived.shapes, [&derived](const ChartShape& shape) {
        return derived.postures[shape.posture].stops[3] == std::optional{fretStop(6)};
    });
    REQUIRE(grip != derived.shapes.end());
    // The chord at 2:1.
    CHECK(grip->place == Fraction{1});
}

// A slide-out's finger stays pressed while it slides, so a chord sliding out from its strike keeps
// its span and grip to the slide-out keyframe at the ring's end.
TEST_CASE("A span's grip holds to a slide-out's keyframe", "[core][chart]")
{
    const auto sliding_out = [](const int string, const int fret) {
        ChartNote note = noteAt(1, {}, string, fret, Fraction{1, 4});
        setSlideOut(note, fret - 3);
        return note;
    };
    const ChartShapes derived = deriveFrom({sliding_out(1, 5), sliding_out(2, 7)});
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sustain == Fraction{1, 4});
    CHECK(derivedStops(derived, 0)[0] == std::optional{fretStop(5)});
    CHECK(derivedStops(derived, 0)[1] == std::optional{fretStop(7)});
}

// Shape rule 4 judges a landing once, before the slot at its instant: the 7 sliding onto 5 lands
// as the open string beside it is restruck, and that open ring ends exactly at the landing, so one
// survivor opens nothing. The restrike rides the span at its slot and never makes the same landing
// a hand-off at a later slot.
TEST_CASE("A landing is judged once, before a restrike at its instant", "[core][chart]")
{
    const ChartShapes derived = deriveFrom(streamOf({
        noteAt(3, {}, 2, 0, Fraction{1, 8}),
        travellingAt(noteAt(3, {}, 3, 7, Fraction{1, 4}), {{Fraction{1, 8}, 5}}),
        noteAt(3, Fraction{1, 2}, 2, 0, Fraction{1, 8}),
        noteAt(4, {}, 2, 0, Fraction{1, 8}),
    }));

    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{1, 2});
    CHECK(derived.shapes[0].sustain == Fraction{1, 8});
    CHECK_FALSE(derived.shapes[0].landing_opened);
}

// Inside a hold only a hand conflict breaks the span: a higher fret restated on a stated string is
// another finger, so the notation split the derivation makes there is suppressed, and the hold's
// span keeps the stop first stated.
TEST_CASE("A shape hold is one span across a notation split", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(gridAt(1, 2), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(1, 2), 2, 9, Fraction{1, 4}),
    });
    REQUIRE(deriveUnder(notes, {}).resolutions.shapes.size() == 2);

    const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 3))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].hold_stated);
    CHECK(held.resolutions.shapes[0].place == Fraction{});
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
    CHECK(stopOf(held.resolutions, 0, 2) == std::optional{fretStop(7)});
    CHECK(held.fits[0]);
}

// A hand conflict inside a hold crops its span there: a lower fret releases the stated stop
// (hand-position rule 6), so the published extent falls short of the stated one.
TEST_CASE("A hand conflict crops a shape hold's span", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(gridAt(1, 2), 1, 3, Fraction{1, 4}),
        noteAt(gridAt(1, 2), 2, 7, Fraction{1, 4}),
    });
    const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 3))});
    REQUIRE_FALSE(held.resolutions.shapes.empty());
    CHECK(held.resolutions.shapes[0].hold_stated);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 4});
    CHECK_FALSE(held.fits[0]);
}

// At or after its stated end the span closes wherever the walk would close a span standing there: a
// hold ending where nothing closes one does not fit its notes, so the span runs on to the first
// close; a hold ending where a stated stop has stopped sounding, at the next hold's start or at the
// song's end fits.
TEST_CASE("A shape hold's end must be a break", "[core][chart]")
{
    const std::vector<ChartNote> chord = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 2}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 2}),
    });

    SECTION("an end in the middle of a sustain does not fit, the span running to the break")
    {
        const Held held = deriveUnder(chord, {holdOver(gridAt(1, 1), gridAt(1, 2))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
        CHECK_FALSE(held.fits[0]);
    }

    SECTION("an end where one stated stop has stopped sounding fits, the rest ringing on")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{1, 2}),
        });
        CHECK(deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))}).fits[0]);
    }

    SECTION("the next hold's start is a break")
    {
        const Held held = deriveUnder(
            chord, {holdOver(gridAt(1, 1), gridAt(1, 2)), holdOver(gridAt(1, 2), gridAt(1, 3))});
        CHECK(held.fits[0]);
        CHECK(held.fits[1]);
    }

    SECTION("the song's end is a break")
    {
        const std::vector<ChartNote> last = streamOf({
            noteAt(gridAt(8, 1), 1, 5, Fraction{2}),
            noteAt(gridAt(8, 1), 2, 7, Fraction{2}),
        });
        CHECK(deriveUnder(last, {holdOver(gridAt(8, 1), gridAt(9, 1))}).fits[0]);
    }

    SECTION("a finger landing on a string ringing open is a break")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 2}),
            noteAt(gridAt(1, 1), 2, 0, Fraction{1, 4}),
            noteAt(gridAt(1, 2), 2, 4, Fraction{1, 4}),
        });
        CHECK(deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))}).fits[0]);
    }

    SECTION(
        "a member restruck exactly at the end closes the span there: the end is judged over "
        "the hold alone, and the restrike belongs to what follows")
    {
        // An arpeggio accumulates over 1:1 to 1:3; the fret-5 ring ends at 1:4, where it is
        // re-picked. Without the hold the re-pick renews it and the span runs to 2:1 (rule 3).
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 3, 9, Fraction{1}),
            noteAt(gridAt(1, 2), 2, 7, Fraction{3, 4}),
            noteAt(gridAt(1, 3), 1, 5, Fraction{1, 4}),
            noteAt(gridAt(1, 4), 1, 5, Fraction{1, 4}),
        });
        const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
        REQUIRE(plain.shapes.size() == 1);
        REQUIRE(plain.shapes[0].sustain == Fraction{1});

        // Under a hold ending at 1:4 the re-pick is outside it, so the span closes there and the
        // hold fits. The re-pick founds nothing of its own over the two rings still sounding:
        // they were struck behind the close, and one statement is no accumulation (rule 1), so
        // the hold's span is the figure's only one.
        const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 4))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].hold_stated);
        CHECK(held.resolutions.shapes[0].place == Fraction{});
        CHECK(held.resolutions.shapes[0].sustain == Fraction{3, 4});
        CHECK(held.fits[0]);
    }

    SECTION(
        "a tap renewing the rings at the end leaves the end where the fretting hand's rings end")
    {
        ChartNote low_tap = noteAt(gridAt(1, 2), 1, 12, Fraction{1, 4});
        low_tap.attack = NoteAttack::Tap;
        ChartNote high_tap = noteAt(gridAt(1, 2), 2, 14, Fraction{1, 4});
        high_tap.attack = NoteAttack::Tap;
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
            low_tap,
            high_tap,
        });
        const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
        REQUIRE(plain.shapes.size() == 1);
        REQUIRE(plain.shapes[0].sustain == Fraction{1, 4});
        CHECK(deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))}).fits[0]);
    }
}

// A superset restatement — the grip restruck whole with a finger added on a silent string — splits
// the derived span, while a hold across it keeps one span; the grip was stated only in part at the
// hold's start, so the span sounds in parts. The notes after the hold derive as with no hold.
TEST_CASE("A shape hold keeps one arpeggio across a superset restatement", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 3, Fraction{1, 4}),
        noteAt(gridAt(1, 1), 2, 3, Fraction{1, 4}),
        noteAt(gridAt(1, 2), 1, 3, Fraction{1, 8}),
        noteAt(gridAt(1, 2), 2, 3, Fraction{1, 8}),
        noteAt(gridAt(1, 2), 3, 5, Fraction{1, 8}),
        noteAt(gridAt(1, 2, Fraction{1, 2}), 1, 3, Fraction{1, 8}),
        noteAt(gridAt(1, 2, Fraction{1, 2}), 2, 3, Fraction{1, 8}),
    });
    const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
    REQUIRE(plain.shapes.size() == 3);
    REQUIRE(plain.shapes[1].place == Fraction{1, 4});

    const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 2, Fraction{1, 2}))});
    REQUIRE(held.resolutions.shapes.size() == 2);
    CHECK(held.resolutions.shapes[0].hold_stated);
    CHECK(held.resolutions.shapes[0].place == Fraction{});
    CHECK(held.resolutions.shapes[0].sustain == Fraction{3, 8});
    CHECK(stopOf(held.resolutions, 0, 3) == std::optional{fretStop(5)});
    CHECK(held.resolutions.arrivals[0]);
    // The posture table orders its rows by first sighting, so the after-span's row is compared by
    // what it holds rather than by its index.
    ChartShape after = held.resolutions.shapes[1];
    CHECK(held.resolutions.postures[after.posture] == plain.postures[plain.shapes[2].posture]);
    after.posture = plain.shapes[2].posture;
    CHECK(after == plain.shapes[2]);
    CHECK(held.fits[0]);
}

// A finger landing on a string ringing open closes the derived span, and is notation inside a
// hold: the span stays whole.
TEST_CASE("A shape hold spans a finger landing on an open ring", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 2}),
        noteAt(gridAt(1, 1), 2, 0, Fraction{1, 4}),
        noteAt(gridAt(1, 2), 2, 4, Fraction{1, 4}),
    });
    const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
    REQUIRE(plain.shapes.size() == 1);
    REQUIRE(plain.shapes[0].sustain == Fraction{1, 4});
    const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 3))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
    CHECK(held.fits[0]);
}

// Rests are legal inside a hold: a member's silence is notation, so the span runs on over the rest
// and the restrike after it, and a hold ending in a rest ends where its statement is no longer in
// force.
TEST_CASE("A shape hold spans a rest", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(gridAt(1, 3), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(1, 3), 2, 7, Fraction{1, 4}),
    });
    REQUIRE(deriveUnder(notes, {}).resolutions.shapes.size() == 2);

    SECTION("a hold over the rest is one span and fits")
    {
        const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(2, 1))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1});
        CHECK(held.fits[0]);
    }

    SECTION("a hold ending in the rest fits")
    {
        const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 3))});
        REQUIRE_FALSE(held.resolutions.shapes.empty());
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
        CHECK(held.fits[0]);
    }
}

// A none hold publishes no arpeggio span inside it, while a chord struck inside keeps its box; the
// notes after the hold derive as they do with no hold.
TEST_CASE("A none hold drops an arpeggio span and keeps a chord box", "[core][chart]")
{
    ShapeHold none = holdOver(gridAt(1, 1), gridAt(2, 1));
    none.grip = NoneGrip{};
    const std::vector<ChartNote> after{
        noteAt(gridAt(3, 1), 1, 3, Fraction{1, 4}),
        noteAt(gridAt(3, 1), 2, 5, Fraction{1, 4}),
    };

    SECTION("an arpeggio inside is dropped")
    {
        std::vector<ChartNote> notes{
            noteAt(gridAt(1, 1), 1, 5, Fraction{3, 4}),
            noteAt(gridAt(1, 2), 2, 7, Fraction{1, 2}),
            noteAt(gridAt(1, 3), 3, 7, Fraction{1, 4}),
        };
        notes.insert(notes.end(), after.begin(), after.end());
        notes = streamOf(notes);
        REQUIRE(deriveUnder(notes, {}).resolutions.shapes.size() == 2);
        const Held held = deriveUnder(notes, {none});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].place == Fraction{2});
        CHECK(held.fits[0]);
    }

    SECTION("a chord box inside stays")
    {
        std::vector<ChartNote> notes{
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
        };
        notes.insert(notes.end(), after.begin(), after.end());
        notes = streamOf(notes);
        const Held held = deriveUnder(notes, {none});
        CHECK(held.resolutions.shapes == deriveUnder(notes, {}).resolutions.shapes);
    }
}

// A pull-off source riding the stop the hold's span states, and its release back onto it, are one
// hand: neither is a conflict, so the hold stands.
TEST_CASE("A pull-off inside a shape hold leaves it whole", "[core][chart]")
{
    ChartNote pull = noteAt(gridAt(1, 2, Fraction{1, 2}), 2, 7, Fraction{3, 8});
    pull.attack = NoteAttack::Legato;
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{3, 4}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(gridAt(1, 2), 2, 9, Fraction{1, 8}),
        pull,
    });
    const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 4))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{3, 4});
    CHECK(stopOf(held.resolutions, 0, 2) == std::optional{fretStop(7)});
    CHECK(held.fits[0]);
}

// A member's glide leaving the stop the span states is a hand conflict at its landing (shape rule
// 4), a multi-leg glide landing where it comes to rest; the landed grip opens its successor there.
TEST_CASE("A glide inside a shape hold crops it where it lands", "[core][chart]")
{
    ChartNote gliding = noteAt(gridAt(1, 1), 2, 7, Fraction{1});
    gliding.keyframes = {
        Keyframe{.offset = Fraction{1, 4}, .fret = 9},
        Keyframe{.offset = Fraction{1, 2}, .fret = 11},
    };
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1}),
        gliding,
    });
    const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(2, 1))});
    REQUIRE(held.resolutions.shapes.size() == 2);
    CHECK(held.resolutions.shapes[0].hold_stated);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
    CHECK(held.resolutions.shapes[1].landing_opened);
    CHECK(stopOf(held.resolutions, 1, 2) == std::optional{fretStop(11)});
    CHECK_FALSE(held.fits[0]);
}

// A hold exactly over a derived span changes nothing but marking that span hold-stated, and fits.
TEST_CASE("A shape hold exactly over a derived span derives it unchanged", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 3, Fraction{3, 4}),
        noteAt(gridAt(1, 1), 2, 5, Fraction{3, 4}),
        noteAt(gridAt(1, 4), 1, 3, Fraction{1, 4}),
        noteAt(gridAt(1, 4), 2, 5, Fraction{1, 4}),
        noteAt(gridAt(2, 1), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(2, 1), 2, 7, Fraction{1, 4}),
    });
    const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
    REQUIRE(plain.shapes.size() == 2);
    const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(2, 1))});
    std::vector<ChartShape> expected = plain.shapes;
    expected[0].hold_stated = true;
    CHECK(held.resolutions.shapes == expected);
    CHECK(held.resolutions.postures == plain.postures);
    CHECK(held.resolutions.arrivals == plain.arrivals);
    CHECK(held.fits[0]);
}

namespace
{

// The holds normalized against the notes, asserting the normal form is a fixpoint: normalizing it
// again rewrites nothing, derives the same spans, and leaves every hold fitting its notes.
[[nodiscard]] ShapeHoldNormalization normalizedOnce(
    const std::vector<ChartNote>& notes, const std::vector<ShapeHold>& holds)
{
    const TempoMap tempo_map = makeTempoMap();
    ShapeHoldNormalization normal = normalizeShapeHolds(notes, holds, 0, tempo_map);
    const ShapeHoldNormalization again =
        normalizeShapeHolds(notes, normal.resolved.shape_holds, 0, tempo_map);
    CHECK(again.resolved.shape_holds == normal.resolved.shape_holds);
    CHECK(again.conversions.empty());
    CHECK(again.resolved.shapes == normal.resolved.shapes);
    CHECK(again.resolved.postures == normal.resolved.postures);
    const Held held = deriveUnder(notes, normal.resolved.shape_holds);
    CHECK(std::ranges::all_of(held.fits, [](const bool fits) { return fits; }));
    return normal;
}

} // namespace

// The one rule writes back where the walk publishes the hold's span: back to a hand conflict inside
// it, on past an end where nothing closes, and nowhere for a hold that already fits.
TEST_CASE("The hold normalizer writes back the published extent", "[core][chart]")
{
    SECTION("a hand conflict inside crops the hold to the conflict")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
            noteAt(gridAt(1, 2), 1, 3, Fraction{1, 4}),
            noteAt(gridAt(1, 2), 2, 7, Fraction{1, 4}),
        });
        const ShapeHoldNormalization normal =
            normalizedOnce(notes, {holdOver(gridAt(1, 1), gridAt(1, 3))});
        CHECK(normal.resolved.shape_holds == std::vector{holdOver(gridAt(1, 1), gridAt(1, 2))});
        REQUIRE(normal.conversions.size() == 1);
        CHECK(normal.conversions[0].repair == ChartRepair::UnfitShapeHold);
        CHECK(normal.conversions[0].where == "shape hold 1:1 to 1:2");
    }

    SECTION("an end in the middle of a sustain extends to where the span closes")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 2}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{1, 2}),
        });
        const ShapeHoldNormalization normal =
            normalizedOnce(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))});
        CHECK(normal.resolved.shape_holds == std::vector{holdOver(gridAt(1, 1), gridAt(1, 3))});
        REQUIRE(normal.conversions.size() == 1);
        CHECK(normal.conversions[0].repair == ChartRepair::UnfitShapeHold);
        CHECK(normal.conversions[0].where == "shape hold 1:1 to 1:3");
    }

    SECTION("an extension stops at the next hold's start, so the holds only touch")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{1}),
        });
        const ShapeHoldNormalization normal = normalizedOnce(
            notes, {holdOver(gridAt(1, 1), gridAt(1, 2)), holdOver(gridAt(1, 3), gridAt(2, 1))});
        REQUIRE(normal.resolved.shape_holds.size() == 2);
        CHECK(normal.resolved.shape_holds[0] == holdOver(gridAt(1, 1), gridAt(1, 3)));
        CHECK(normal.resolved.shape_holds[1].start == gridAt(1, 3));
    }

    SECTION("a hold that fits and a none hold are left as they are")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
        });
        ShapeHold none = holdOver(gridAt(2, 1), gridAt(2, 2));
        none.grip = NoneGrip{};
        const std::vector<ShapeHold> holds{holdOver(gridAt(1, 1), gridAt(1, 2)), none};
        const ShapeHoldNormalization normal = normalizedOnce(notes, holds);
        CHECK(normal.resolved.shape_holds == holds);
        CHECK(normal.conversions.empty());
    }
}

// A hold over silence states nothing, so its span stands nowhere past its end: it closes there, and
// fits.
TEST_CASE("A shape hold over silence fits and publishes its span", "[core][chart]")
{
    // Nothing sounds inside the hold; a chord struck where it ends derives its own span.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(3, 1), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(3, 1), 2, 7, Fraction{1, 4}),
    });
    const Held held = deriveUnder(notes, {holdOver(gridAt(2, 1), gridAt(3, 1))});
    REQUIRE(held.resolutions.shapes.size() == 2);
    CHECK(held.resolutions.shapes[0].hold_stated);
    CHECK(held.resolutions.shapes[0].place == Fraction{1});
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1});
    CHECK(
        std::ranges::none_of(
            held.resolutions.postures[held.resolutions.shapes[0].posture].stops,
            [](const std::optional<ChartStop>& stop) { return stop.has_value(); }));
    CHECK_FALSE(held.resolutions.shapes[1].hold_stated);
    CHECK(held.resolutions.shapes[1].place == Fraction{2});
    CHECK(held.fits[0]);
}

// A hold whose notes never found a span stands its span at its end over what rings into it, and
// the walk closes it by its one rule: over a single note's whole ring the hold fits, and ending in
// the middle of that ring, with nothing else happening, it does not.
TEST_CASE("A shape hold over a single note closes where the note's ring ends", "[core][chart]")
{
    const std::vector<ChartNote> notes{noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4})};

    SECTION("from the onset to the ring's end the hold fits")
    {
        const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].hold_stated);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 4});
        CHECK(stopOf(held.resolutions, 0, 1) == std::optional{fretStop(5)});
        CHECK(held.fits[0]);
    }

    SECTION("ending in the middle of the ring it does not, and normalizes to the ring's end")
    {
        const std::vector<ShapeHold> holds{holdOver(gridAt(1, 1), gridAt(1, 1, Fraction{1, 2}))};
        const Held held = deriveUnder(notes, holds);
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 4});
        CHECK_FALSE(held.fits[0]);
        const ShapeHoldNormalization normal = normalizedOnce(notes, holds);
        CHECK(normal.resolved.shape_holds == std::vector{holdOver(gridAt(1, 1), gridAt(1, 2))});
        REQUIRE(normal.conversions.size() == 1);
        CHECK(normal.conversions[0].repair == ChartRepair::UnfitShapeHold);
    }

    SECTION("past the ring's end, where the hand holds nothing, the hold fits")
    {
        const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(2, 1))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1});
        CHECK(held.fits[0]);
    }
}

// A hold's span opens at the hold's start with no founding threshold: the rings carried into the
// start join it as a founding takes them (shape rule 1), every fretting-hand strike inside grows
// it, and it closes by the end rule, so one normalization settles the hold.
TEST_CASE("A hold's span opens at its start over what rings into it", "[core][chart]")
{
    SECTION("a ring carried into the start is a member, and its death at the end closes the span")
    {
        // The open string rings to 1:3 and the fret-5 note to 1:4; neither founds a span. The
        // hold's span carries the open string in and closes where it dies; the ring left standing
        // past the hold is a tail, since the span held no stop of its own.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 6, 0, Fraction{1, 2}),
            noteAt(gridAt(1, 2), 3, 5, Fraction{1, 2}),
        });
        REQUIRE(deriveUnder(notes, {}).resolutions.shapes.empty());
        const ShapeHoldNormalization normal =
            normalizedOnce(notes, {holdOver(gridAt(1, 2), gridAt(1, 2, Fraction{1, 2}))});
        CHECK(normal.resolved.shape_holds == std::vector{holdOver(gridAt(1, 2), gridAt(1, 3))});
        REQUIRE(normal.resolved.shapes.size() == 2);
        CHECK(normal.resolved.shapes[0].hold_stated);
        CHECK(stopOf(normal.resolved, 0, 6) == std::optional{fretStop(0)});
        CHECK(stopOf(normal.resolved, 0, 3) == std::optional{fretStop(5)});
        // The fret-5 strike is the span's own, so the remainder runs on with it past the hold.
        CHECK_FALSE(normal.resolved.shapes[1].hold_stated);
        CHECK(normal.resolved.shapes[1].place == Fraction{1, 2});
        CHECK(normal.resolved.shapes[1].sustain == Fraction{1, 4});
    }

    SECTION("a hold over carried rings alone closes where they die, leaving no remainder")
    {
        // The chord rings to 1:3 on one string and 1:4 on the other; the hold states what rings
        // into it and nothing of its own, so the ring past the first death is a tail.
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{1, 2}),
            noteAt(gridAt(1, 1), 2, 7, Fraction{3, 4}),
        });
        const std::vector<ShapeHold> holds{
            holdOver(gridAt(1, 1), gridAt(1, 2)), holdOver(gridAt(1, 2), gridAt(1, 3))
        };
        const Held held = deriveUnder(notes, holds);
        REQUIRE(held.resolutions.shapes.size() == 2);
        CHECK(held.resolutions.shapes[1].hold_stated);
        CHECK(held.resolutions.shapes[1].place == Fraction{1, 4});
        CHECK(held.resolutions.shapes[1].sustain == Fraction{1, 4});
        CHECK(stopOf(held.resolutions, 1, 1) == std::optional{fretStop(5)});
        CHECK(stopOf(held.resolutions, 1, 2) == std::optional{fretStop(7)});
        CHECK(held.fits[0]);
        CHECK(held.fits[1]);
    }

    SECTION("a note struck inside that died before the end is a member whose quit closes the end")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 1, 5, Fraction{3, 4}),
            noteAt(gridAt(1, 2), 2, 7, Fraction{1, 8}),
        });
        const std::vector<ShapeHold> holds{holdOver(gridAt(1, 1), gridAt(1, 3))};
        const Held held = deriveUnder(notes, holds);
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
        CHECK(stopOf(held.resolutions, 0, 1) == std::optional{fretStop(5)});
        CHECK(stopOf(held.resolutions, 0, 2) == std::optional{fretStop(7)});
        CHECK(held.resolutions.arrivals[0]);
        CHECK(held.fits[0]);
        CHECK(normalizedOnce(notes, holds).conversions.empty());
    }

    SECTION("a glide landing past the end closes the span at the landing, its departure stop held")
    {
        // The fret-5 note glides to 7 over 1:3 to 1:3+1/2; the landing hands the grip on.
        const std::vector<ChartNote> notes = streamOf({
            travellingAt(
                noteAt(gridAt(1, 1), 1, 5, Fraction{1}),
                {{Fraction{1, 2}, 5}, {Fraction{5, 8}, 7}}),
            noteAt(gridAt(1, 1, Fraction{1, 2}), 2, 7, Fraction{1}),
        });
        const ShapeHoldNormalization normal =
            normalizedOnce(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))});
        CHECK(
            normal.resolved.shape_holds ==
            std::vector{holdOver(gridAt(1, 1), gridAt(1, 3, Fraction{1, 2}))});
        REQUIRE(normal.resolved.shapes.size() == 2);
        CHECK(stopOf(normal.resolved, 0, 1) == std::optional{fretStop(5)});
        CHECK(normal.resolved.shapes[1].landing_opened);
        CHECK(stopOf(normal.resolved, 1, 1) == std::optional{fretStop(7)});
    }
}

// Inside an unnamed hold the walk asks no founding threshold: a bar of single notes a half note
// each, with a ring carried in from the bar before, is one span of all its stops fronted at the
// hold's start, an arpeggio at any member count since it sounds in parts, and the hold fits. The
// same bar derives nothing without the hold.
TEST_CASE("A hold over a bar of single notes is one arpeggio of all its stops", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 4), 2, 2, Fraction{1, 2}),
        noteAt(gridAt(2, 1), 3, 4, Fraction{1, 2}),
        noteAt(gridAt(2, 2), 1, 3, Fraction{1, 2}),
        noteAt(gridAt(2, 3), 4, 0, Fraction{1, 2}),
        noteAt(gridAt(2, 4), 1, 3, Fraction{1, 2}),
    });
    REQUIRE(deriveUnder(notes, {}).resolutions.shapes.empty());

    const Held held = deriveUnder(notes, {holdOver(gridAt(2, 1), gridAt(3, 1))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].hold_stated);
    CHECK(held.resolutions.shapes[0].place == Fraction{1});
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1});
    CHECK(held.resolutions.shapes[0].bracket_place == std::optional{Fraction{1}});
    CHECK(stopOf(held.resolutions, 0, 1) == std::optional{fretStop(3)});
    CHECK(stopOf(held.resolutions, 0, 2) == std::optional{fretStop(2)});
    CHECK(stopOf(held.resolutions, 0, 3) == std::optional{fretStop(4)});
    CHECK(stopOf(held.resolutions, 0, 4) == std::optional{fretStop(0)});
    CHECK(held.resolutions.arrivals[0]);
    CHECK(held.fits[0]);
}

// A span an unnamed hold states is an arpeggio whenever it sounds in parts, at any member count
// (shape rule 5): a two-note grip re-picked in part breaks as a derived span, and under a hold it
// is one arpeggio span; a hold over a single note's whole ring, struck once, stays a box.
TEST_CASE("A held span of fewer than three members is an arpeggio in parts", "[core][chart]")
{
    SECTION("a two-note grip re-picked in part is one arpeggio under a hold")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(gridAt(1, 1), 3, 0, Fraction{1, 8}),
            noteAt(gridAt(1, 1), 6, 2, Fraction{1, 4}),
            noteAt(gridAt(1, 1, Fraction{1, 2}), 3, 0, Fraction{1, 8}),
        });
        const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
        REQUIRE(plain.shapes.size() == 1);
        REQUIRE(plain.shapes[0].sustain == Fraction{1, 8});
        REQUIRE(plain.arrivals == std::vector<bool>{false});

        const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].hold_stated);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 4});
        CHECK(held.resolutions.shapes[0].sounds_in_parts);
        CHECK(held.resolutions.arrivals == std::vector<bool>{true});
        CHECK(held.fits[0]);
    }

    SECTION("a single note struck once under a hold is the shape whole, a box")
    {
        const std::vector<ChartNote> notes{noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4})};
        const Held held = deriveUnder(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK_FALSE(held.resolutions.shapes[0].sounds_in_parts);
        CHECK(held.resolutions.arrivals == std::vector<bool>{false});
    }
}

// Hand-position rule 6's release inside a hold crops its span, a glide's release at its landing;
// the cropped hold's end is then a close the walk makes there, so one normalization settles it.
TEST_CASE("A hold cropped at a release normalizes once", "[core][chart]")
{
    // At 1:2 string 1 sounds fret 7, above the stated 5, and glides down to 3: its landing at
    // 1:2+1/2 releases the 5.
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1}),
        travellingAt(noteAt(gridAt(1, 2), 1, 7, Fraction{1, 4}), {{Fraction{1, 8}, 3}}),
    });
    const ShapeHoldNormalization normal =
        normalizedOnce(notes, {holdOver(gridAt(1, 1), gridAt(1, 3))});
    CHECK(
        normal.resolved.shape_holds ==
        std::vector{holdOver(gridAt(1, 1), gridAt(1, 2, Fraction{1, 2}))});
    REQUIRE(normal.conversions.size() == 1);
    CHECK(normal.conversions[0].repair == ChartRepair::UnfitShapeHold);
}

// A hold's wall is its published end. Here the first hold's span closes at a conflict, and the span
// struck there runs on past the hold's stated end to a two-finger glide landing exactly where the
// next hold starts, so the next hold states the landed grip and closes where a lower fret releases
// it: one derivation already holds both closes, and one normalization settles both holds.
TEST_CASE(
    "A crop's stated end walls nothing, so one derivation settles the next hold", "[core][chart]")
{
    ChartNote low = noteAt(gridAt(1, 1, Fraction{1, 2}), 1, 3, Fraction{1, 2});
    low.keyframes = {Keyframe{.offset = Fraction{3, 8}, .fret = 5}};
    ChartNote high = noteAt(gridAt(1, 1, Fraction{1, 2}), 3, 5, Fraction{5, 8});
    high.keyframes = {Keyframe{.offset = Fraction{3, 8}, .fret = 7}};
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 8}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 8}),
        low,
        high,
        noteAt(gridAt(1, 3, Fraction{1, 2}), 1, 2, Fraction{1, 8}),
    });
    const std::vector<ShapeHold> holds{
        holdOver(gridAt(1, 1), gridAt(1, 2, Fraction{1, 2})),
        holdOver(gridAt(1, 3), gridAt(2, 1)),
    };
    // Under the stated holds the later hold already closes short of its end.
    REQUIRE_FALSE(deriveUnder(notes, holds).fits[1]);

    const ShapeHoldNormalization normal = normalizedOnce(notes, holds);
    CHECK(
        normal.resolved.shape_holds == std::vector{
                                           holdOver(gridAt(1, 1), gridAt(1, 1, Fraction{1, 2})),
                                           holdOver(gridAt(1, 3), gridAt(1, 3, Fraction{1, 2})),
                                       });
    REQUIRE(normal.conversions.size() == 2);
    CHECK(normal.conversions[0].where == "shape hold 1:1 to 1:1+1/2");
    CHECK(normal.conversions[1].where == "shape hold 1:3 to 1:3+1/2");
    const Held held = deriveUnder(notes, normal.resolved.shape_holds);
    REQUIRE(held.resolutions.shapes.size() == 3);
    CHECK(held.resolutions.shapes[2].landing_opened);
    CHECK(held.resolutions.shapes[2].hold_stated);
}

// Nothing sounds past the song's end, so a hold a re-address left starting there closes at its own
// start and is dropped; the hold before it fits at the song's end.
TEST_CASE("The hold normalizer drops a hold that closes at its own start", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(8, 1), 1, 5, Fraction{2}),
        noteAt(gridAt(8, 1), 2, 7, Fraction{2}),
    });
    const ShapeHoldNormalization normal = normalizedOnce(
        notes, {holdOver(gridAt(8, 1), gridAt(9, 1)), holdOver(gridAt(9, 1), gridAt(9, 2))});
    CHECK(normal.resolved.shape_holds == std::vector{holdOver(gridAt(8, 1), gridAt(9, 1))});
    REQUIRE(normal.conversions.size() == 1);
    CHECK(normal.conversions[0].repair == ChartRepair::EmptyShapeHold);
    CHECK(normal.conversions[0].where == "shape hold 9:1 to 9:2");
}

// A re-address can round a hold onto no length; the walk cannot place it, so it is dropped and
// reported, and the holds beside it are judged without it.
TEST_CASE("The hold normalizer drops a hold left no length", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 4}),
    });
    const ShapeHoldNormalization normal = normalizedOnce(
        notes, {holdOver(gridAt(1, 1), gridAt(1, 2)), holdOver(gridAt(1, 3), gridAt(1, 3))});
    CHECK(normal.resolved.shape_holds == std::vector{holdOver(gridAt(1, 1), gridAt(1, 2))});
    REQUIRE(normal.conversions.size() == 1);
    CHECK(normal.conversions[0].repair == ChartRepair::EmptyShapeHold);
    CHECK(normal.conversions[0].where == "shape hold 1:3 to 1:3");
}

// A re-address that rounds an unnamed hold onto no length between two none holds leaves the two
// meeting: the emptied hold goes first, so they merge in the same pass, and a second pass changes
// nothing.
TEST_CASE("The hold normalizer merges none holds an emptied hold kept apart", "[core][chart]")
{
    const auto none = [](const GridPosition start, const GridPosition end) {
        return ShapeHold{.start = start, .end = end, .grip = NoneGrip{}};
    };
    const ShapeHoldNormalization normal = normalizedOnce(
        {},
        {none(gridAt(1, 1), gridAt(2, 1)),
         holdOver(gridAt(2, 1), gridAt(2, 1)),
         none(gridAt(2, 1), gridAt(3, 1))});
    CHECK(normal.resolved.shape_holds == std::vector{none(gridAt(1, 1), gridAt(3, 1))});
    REQUIRE(normal.conversions.size() == 2);
    CHECK(normal.conversions[0].repair == ChartRepair::EmptyShapeHold);
    CHECK(normal.conversions[1].repair == ChartRepair::MergedNoneHolds);
    CHECK(normal.conversions[1].where == "shape hold 1:1 to 3:1");
}

// A none hold lying inside another merges into it, which leaves the outer hold's edges as they
// were: the merge is still a rewrite, so it is reported.
TEST_CASE("The hold normalizer reports a none hold merged into one around it", "[core][chart]")
{
    const auto none = [](const GridPosition start, const GridPosition end) {
        return ShapeHold{.start = start, .end = end, .grip = NoneGrip{}};
    };
    const ShapeHoldNormalization normal =
        normalizedOnce({}, {none(gridAt(1, 1), gridAt(4, 1)), none(gridAt(2, 1), gridAt(3, 1))});
    CHECK(normal.resolved.shape_holds == std::vector{none(gridAt(1, 1), gridAt(4, 1))});
    REQUIRE(normal.conversions.size() == 1);
    CHECK(normal.conversions[0].repair == ChartRepair::MergedNoneHolds);
    CHECK(normal.conversions[0].where == "shape hold 1:1 to 4:1");
}

// The notes layer completed from the normalizer's own derivation is the one a plain derivation
// under the normalized holds gives, and records those holds.
TEST_CASE("The normalized notes layer is the layer under the normalized holds", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNote> notes = streamOf({
        noteAt(gridAt(1, 1), 1, 5, Fraction{1, 2}),
        noteAt(gridAt(1, 1), 2, 7, Fraction{1, 2}),
        noteAt(gridAt(2, 1), 1, 3, Fraction{1, 4}),
        noteAt(gridAt(2, 1), 2, 5, Fraction{1, 4}),
    });
    const ShapeHoldNormalization normal =
        normalizeShapeHolds(notes, {holdOver(gridAt(1, 1), gridAt(1, 2))}, 0, tempo_map);
    const std::vector<ShapeHold> expected{holdOver(gridAt(1, 1), gridAt(1, 3))};
    CHECK(normal.resolved.shape_holds == expected);
    CHECK(normal.conversions.size() == 1);
    const ChartResolutions plain = chartResolutions(notes, expected, 0, tempo_map);
    CHECK(normal.resolved.shapes == plain.shapes);
    CHECK(normal.resolved.postures == plain.postures);
    CHECK(normal.resolved.held_stops == plain.held_stops);
    CHECK(normal.resolved.note_lengths == plain.note_lengths);
}

// A span opens at a statement and runs forward from it (shape rule 1): a statement made before the
// front is carried and founds nothing, so a figure whose own statements since the front are two
// derives nothing — however many rings sound under them. The sighted bar: an open string struck
// and ringing, a foreign fret on another string displaced by that string's own grip at beat 2, a
// third string at beat 3, and the open string restruck at a fret at beat 4.
TEST_CASE("A figure with two statements since its front derives no span", "[core][chart]")
{
    const ChartShapes derived = deriveFrom(streamOf({
        noteAt(4, Fraction{1, 2}, 5, 3, Fraction{3, 8}),
        inMeasure(2, noteAt(1, Fraction{1, 2}, 3, 0, Fraction{5, 8})),
        inMeasure(2, noteAt(2, Fraction{}, 5, 2, Fraction{3, 4})),
        inMeasure(2, noteAt(3, Fraction{}, 4, 2, Fraction{1, 2})),
        inMeasure(2, noteAt(4, Fraction{}, 3, 4, Fraction{1, 4})),
    }));
    // The 2 displaces the 3 and breaks the span the open string opened; its own span carries the
    // open string, and gains the 2 on string 4: two own statements, under both thresholds.
    CHECK(derived.shapes.empty());
}

// A pedal that opened a span is its first member (shape rule 1): a whole stroke struck over it
// while it rings grows the span, which is founded by the stroke's two stops and brackets from the
// pedal, sounding in parts.
TEST_CASE("A pedal followed by a whole stroke brackets from the pedal", "[core][chart]")
{
    const ChartShapes derived = deriveFrom(streamOf({
        noteAt(1, Fraction{}, 6, 0, Fraction{1}),
        noteAt(3, Fraction{}, 5, 2, Fraction{1, 2}),
        noteAt(3, Fraction{}, 4, 2, Fraction{1, 2}),
    }));
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sustain == Fraction{1});
    CHECK(derived.shapes[0].bracket_place == std::optional{Fraction{}});
    CHECK(derived.shapes[0].sounds_in_parts);
    CHECK(memberCount(derived, 0) == 3);
    CHECK(derivedStops(derived, 0)[5] == std::optional{fretStop(0)});
    everySpanIsPositive(derived);
}

// The absorption rule (shape rule 3) needs every member the parts do not restate to sound past the
// parts: where the others' rings end at the re-pick, the quit arm closes the span there, and the
// stroke stood alone as the chord it was. One ringing member does not make a part sound under a
// stroke whose other strings have gone silent.
TEST_CASE("A stroke whose other members end at the re-pick is a box", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(1, Fraction{}, 2, 4, Fraction{1, 2}),
        noteAt(1, Fraction{}, 3, 4, Fraction{1, 4}),
        noteAt(1, Fraction{}, 4, 2, Fraction{1, 4}),
        noteAt(2, Fraction{}, 3, 4, Fraction{1, 4}),
    });
    const ChartShapes derived = deriveFrom(notes);
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sustain == Fraction{1, 4});
    CHECK_FALSE(derived.shapes[0].sounds_in_parts);
    CHECK(arpeggiosFrom(notes) == std::vector<bool>{false});

    // The control: the other members ring past the re-pick, so the parts sound under the stroke,
    // which is absorbed, and the span flows through them in parts.
    const std::vector<ChartNote> ringing = streamOf({
        noteAt(1, Fraction{}, 2, 4, Fraction{1, 2}),
        noteAt(1, Fraction{}, 3, 4, Fraction{1, 2}),
        noteAt(1, Fraction{}, 4, 2, Fraction{1, 2}),
        noteAt(2, Fraction{}, 3, 4, Fraction{1, 4}),
    });
    const ChartShapes flowing = deriveFrom(ringing);
    REQUIRE(flowing.shapes.size() == 1);
    CHECK(flowing.shapes[0].sustain == Fraction{1, 2});
    CHECK(flowing.shapes[0].sounds_in_parts);
}

// A ring that opened a span prints in it (shape rule 1), and a natural harmonic struck over that
// open string displaces it and closes the span there (shape rule 3): the open string's tenure is
// visible for exactly as long as it was the hand's.
TEST_CASE("An open ring that opened the span prints until a harmonic replaces it", "[core][chart]")
{
    ChartNote chime = noteAt(4, Fraction{}, 5, 0, Fraction{1, 4});
    chime.harmonic_node = 12.0;
    const ChartShapes derived = deriveFrom(streamOf({
        noteAt(1, Fraction{}, 5, 0, Fraction{3, 4}),
        noteAt(2, Fraction{}, 3, 0, Fraction{1}),
        noteAt(3, Fraction{}, 1, 0, Fraction{1}),
        chime,
        noteAt(4, Fraction{1, 2}, 4, 0, Fraction{1, 2}),
    }));
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes[0].place == Fraction{});
    CHECK(derived.shapes[0].sustain == Fraction{3, 4});
    CHECK(derived.shapes[0].closing_onset_place == std::optional{Fraction{3, 4}});
    CHECK(memberCount(derived, 0) == 3);
    CHECK(derivedStops(derived, 0)[4] == std::optional{fretStop(0)});
    everySpanIsPositive(derived);
}

// A span with no member of its own has nothing to arrive separately (shape rule 5): a hold over
// silence under a tapped line publishes its empty-grip span as a box, however the other hand
// sounds inside it.
TEST_CASE("A held span of no members under taps is a box", "[core][chart]")
{
    ChartNote low = noteAt(gridAt(1, 1), 1, 12, Fraction{1, 4});
    low.attack = NoteAttack::Tap;
    ChartNote high = noteAt(gridAt(1, 2), 1, 14, Fraction{1, 4});
    high.attack = NoteAttack::Tap;
    const Held held = deriveUnder(streamOf({low, high}), {holdOver(gridAt(1, 1), gridAt(2, 1))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].hold_stated);
    CHECK(
        std::ranges::none_of(
            held.resolutions.postures[held.resolutions.shapes[0].posture].stops,
            [](const std::optional<ChartStop>& stop) { return stop.has_value(); }));
    CHECK(held.resolutions.arrivals == std::vector<bool>{false});
    CHECK(held.fits[0]);
}

} // namespace rock_hero::common::core
