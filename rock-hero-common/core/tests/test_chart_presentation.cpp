#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// A plain 4/4 map with room for the handful of measures these fixtures use. In 4/4 the margin is a
// quarter beat (1/16 of a whole note). The kept-sustain bound is
// `g_minimum_kept_sustain_whole_note` resolved at this meter — stated there and nowhere else,
// since it is headed for a user-tunable option — and rule 3 compares against it STRICTLY: a ring
// landing exactly ON it presents no tail (user ruling 2026-09-07). The cases below say where each
// fixture's ring sits relative to the bound rather than restating the value.
[[nodiscard]] TempoMap fourFourMap()
{
    return TempoMap::defaultMap(TimeDuration{60.0});
}

// Measures 1-2 are 4/4, measure 3 onward is 6/8. Both bounds are whole-note-referenced, so the
// meter change doubles them together: the margin goes from 1/4 beat to 1/2 beat, and the
// kept-sustain bound doubles beside it. That is what makes a 6/8 trim a different number, not a
// different rule.
[[nodiscard]] TempoMap meterChangeMap()
{
    return TempoMap{
        {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
         TimeSignatureChange{.measure = 3, .numerator = 6, .denominator = 8}},
        {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
         BeatAnchor{.measure = 8, .beat = 1, .seconds = 24.0}},
    };
}

// A plain 12/8 map. One signature beat here is an eighth note — the meter the bound's note-value
// reference exists for, since a beat-referenced bound would hand nearly every note of a 12/8 song
// a tail. The bound's own case below is what reads it.
[[nodiscard]] TempoMap twelveEightMap()
{
    return TempoMap{
        {TimeSignatureChange{.measure = 1, .numerator = 12, .denominator = 8}},
        {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
         BeatAnchor{.measure = 5, .beat = 1, .seconds = 24.0}},
    };
}

// Spells a grid position, so the fixtures below read as musical addresses rather than aggregates.
[[nodiscard]] GridPosition at(const int measure, const int beat, const Fraction offset = {})
{
    return GridPosition{.measure = measure, .beat = beat, .offset = offset};
}

// One plain note. The aggregate is spelled here once because `position`, `bend` and `slides` carry
// no default member initializer, so every construction site would otherwise have to name them;
// callers set the technique fields they care about on the returned value.
[[nodiscard]] ChartNote note(
    const GridPosition position, const int string, const Fraction sustain, const int fret = 5)
{
    return ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = sustain,
        .bend = 0.0,
        .keyframes = {},
    };
}

// A silently-held stop: a fretting-hand stop the pick never reached, so it draws no head and
// sounds nothing. Rule 1 has to read past one, which is why two test cases below build them.
[[nodiscard]] ChartNote heldStop(const GridPosition position, const int string, const int fret)
{
    ChartNote held = note(position, string, Fraction{}, fret);
    held.attack = NoteAttack::None;
    return held;
}

// A note that CLAIMS a connection to its same-string predecessor. The claim is the whole of what
// the junction skip reads: intent is what the chart stores, and the direction a claim plays as is
// derived per read from the predecessor's released fret.
[[nodiscard]] ChartNote connected(
    const GridPosition position, const int string, const Fraction sustain, const int fret)
{
    ChartNote claiming = note(position, string, sustain, fret);
    claiming.attack = NoteAttack::Legato;
    return claiming;
}

// A note whose fret channel TRAVELS: the same note with fret statements added along its ring,
// listed as (offset, fret) pairs so a figure reads as the path the hand takes.
[[nodiscard]] ChartNote travellingAt(
    ChartNote note, const std::vector<std::pair<Fraction, int>>& path)
{
    for (const auto& [offset, fret] : path)
    {
        note.keyframes.push_back(Keyframe{.offset = offset, .fret = fret});
    }
    return note;
}

// A whole figure's picture, DERIVED the way every reader gets it: the spans the notes imply, the
// class those spans arrive as, and the presentation rules — the tail law included — over the saved
// stream, which is exactly what \ref chartResolutions runs. `presented` is therefore the stream
// both surfaces draw. The cases that turn on a span's own facts use this rather than handing in a
// shape, because a case stating those facts itself would be stating the very things the rule is a
// question about.
struct SpanFigure
{
    std::vector<ChartNote> presented;
    std::vector<bool> hidden;
    std::vector<ChartShape> shapes;
    std::vector<bool> arrivals;
};

[[nodiscard]] SpanFigure spanFigure(const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    const ChartConnections connections = chartConnections(saved, tempo_map);
    const ChartShapes derived = deriveChartShapes(
        saved, chartClaimedStops(connections), chartPlantedStops(connections), tempo_map);
    SpanFigure figure;
    figure.arrivals = chartShapeArrivals(saved, derived.shapes, tempo_map);
    ChartPresentation presentation = presentedChartNotes(connections, tempo_map);
    figure.presented = std::move(presentation.notes);
    figure.hidden.clear();
    for (const std::optional<Fraction>& rested : presentation.rested_from)
    {
        figure.hidden.push_back(rested.has_value());
    }
    figure.shapes = derived.shapes;
    return figure;
}

// The presented notes: rules 1 through 4 and the tail law's verdict beside them. There is no
// furniture argument any more — the curtain became universal on 2026-09-07 and presentation stopped
// reading spans at all — so every case below asks presentation one way, and only \ref
// holdsUnderSpans still names a span.
[[nodiscard]] std::vector<ChartNote> presentedNotesOf(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    return presentedChartNotes(chartConnections(saved, tempo_map), tempo_map).notes;
}

// The presented sustains alone — the LENGTH half, which the law never moves.
[[nodiscard]] std::vector<Fraction> presentedSustains(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    std::vector<Fraction> sustains;
    for (const ChartNote& presented : presentedNotesOf(saved, tempo_map))
    {
        sustains.push_back(presented.sustain);
    }
    return sustains;
}

// The presented tail of a note standing entirely alone. No later onset binds it, so rule 1 trims
// nothing and rule 2 has nothing to floor: the value returned is rule 3's verdict by itself, which
// is what the bound's own case reads.
[[nodiscard]] Fraction loneTail(const ChartNote& lone, const TempoMap& tempo_map)
{
    // One presented note per input note is \ref presentedChartNotes's contract, so the element is
    // there to read.
    return presentedSustains({lone}, tempo_map).front();
}

// Which tails the law RESTED, beside \ref presentedSustains: a zero sustain is not one fact, and
// every case below that reads a zero has to say which one it means. The PRESENCE of a verdict,
// folded to a bool because most cases ask rest-or-draw; the offset cases read
// \ref restedOffsetsOf instead.
[[nodiscard]] std::vector<bool> hiddenOf(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    const ChartPresentation presentation =
        presentedChartNotes(chartConnections(saved, tempo_map), tempo_map);
    std::vector<bool> rests;
    rests.reserve(presentation.rested_from.size());
    for (const std::optional<Fraction>& rested : presentation.rested_from)
    {
        rests.push_back(rested.has_value());
    }
    return rests;
}

// The verdicts themselves, for the cases about WHERE a tail rests rather than whether it does.
[[nodiscard]] std::vector<std::optional<Fraction>> restedOffsetsOf(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    return presentedChartNotes(chartConnections(saved, tempo_map), tempo_map).rested_from;
}

// The holds a stated span implies, asked the way \ref chartResolutions asks them: the presented
// stream and the law's verdict together, against the stream they came from. The SPAN is still an
// input here, and this is the only helper for which that is true — the hold channel is where
// coverage still means something.
[[nodiscard]] std::vector<Fraction> holdsUnderSpans(
    const std::vector<ChartNote>& saved, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    const ChartConnections connections = chartConnections(saved, tempo_map);
    const ChartPresentation presentation = presentedChartNotes(connections, tempo_map);
    return chartHolds(presentation, connections, shapes, tempo_map);
}

} // namespace

// Rule 1: a tail reaching its next binding onset ends one minimum sustain distance before it, and
// the margin is read at the note's OWN measure — so the same 3-beat ring against a 3-beat gap
// keeps a quarter beat clear in 4/4 and half a beat clear in 6/8.
TEST_CASE("Rule 1 trims a tail to the margin at its own meter", "[core][chart]")
{
    const TempoMap map = meterChangeMap();
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{2}),
        note(at(1, 3), 2, Fraction{1}),
        note(at(3, 1), 1, Fraction{3}),
        note(at(3, 4), 2, Fraction{2}),
    };

    const std::vector<Fraction> presented = presentedSustains(saved, map);
    REQUIRE(presented.size() == saved.size());
    // 4/4: two beats to the next onset, less the quarter-beat margin.
    CHECK(presented[0] == Fraction{7, 4});
    // Six beats of clearance, so nothing trims.
    CHECK(presented[1] == Fraction{1});
    // 6/8: three eighth-note beats to the next onset, less the half-beat margin.
    CHECK(presented[2] == Fraction{5, 2});
    // Nothing binds the last onset, so it presents whole.
    CHECK(presented[3] == Fraction{2});

    // The derivation reads the saved stream and never rewrites it: the stored duration stays the
    // actual ring, which is the whole point of deriving the picture instead of storing it.
    CHECK(saved[0].sustain == Fraction{2});
    CHECK(saved[2].sustain == Fraction{3});
}

// Rule 1's binding onset is the first SOUNDING onset the ring does not PASS, passing meaning
// running strictly past it. So a ring-through keeps looking rather than escaping the trim: it
// binds on the first onset it merely reaches, and only a ring nothing binds presents whole. The
// exemption this replaced switched clipping off for every ring-through, which let a tail die on a
// later head with no gap at all.
TEST_CASE("Rule 1 binds on the first onset a ring does not pass", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a ring-through ending inside the margin trims back to it")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{15, 8}),
            note(at(1, 2), 2, Fraction{1}),
            note(at(1, 3), 3, Fraction{1}),
        };

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The ring passes the onset a beat in, then stops an eighth beat short of the one two
        // beats in — well inside the quarter-beat margin — so that onset binds it and trims to
        // 7/4. Under the exemption it presented its whole 15/8 and died on that head.
        CHECK(presented[0] == Fraction{7, 4});
        CHECK(presented[0] != Fraction{15, 8});
        // The onset it passed still trims against the onset after it, exactly as before.
        CHECK(presented[1] == Fraction{3, 4});
        CHECK(presented[2] == Fraction{1});
    }

    SECTION("a ring ending exactly on a later onset binds there")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 2), 2, Fraction{1}),
            note(at(1, 3), 3, Fraction{1}),
        };

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        // Reaching an onset is not passing it, so the two-beat ring binds on the onset two beats
        // in. This is the common let-ring collision rather than a corner case: a notated ring
        // ends on a musical boundary and the next note starts from one.
        CHECK(presented[0] == Fraction{7, 4});
        CHECK(presented[1] == Fraction{3, 4});
        CHECK(presented[2] == Fraction{1});
    }

    SECTION("a ring no later onset binds presents whole")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 2), 2, Fraction{1}),
            note(at(1, 3), 3, Fraction{1}),
        };

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The whole-bar ring passes both later onsets and nothing lies beyond its end, so nothing
        // binds it — the same answer a last note has always had.
        CHECK(presented[0] == Fraction{4});
        CHECK(presented[1] == Fraction{3, 4});
        CHECK(presented[2] == Fraction{1});
    }

    SECTION("a tail that passes nothing binds on the very next onset")
    {
        // The control that keeps the scan honest: an ordinary tail must read the onset in front of
        // it and stop there, never walk on to a later one.
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1, Fraction{1, 2}), 2, Fraction{1}),
            note(at(1, 2), 3, Fraction{1}),
        };
        // A whole-note technique changes no payload offset, so it only earns the group its tail
        // (rule 3) and leaves rule 1's arithmetic to speak for itself.
        saved[0].vibrato = VibratoState::Narrow;

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        // Half a beat to the next onset, less the quarter-beat margin.
        CHECK(presented[0] == Fraction{1, 4});
        // Binding on the onset a whole beat away instead would have left 3/4.
        CHECK(presented[0] != Fraction{3, 4});
    }

    SECTION("a silent hold past the ring's end binds nothing")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 2), 2, Fraction{1}),
            heldStop(at(1, 3), 4, 7),
            note(at(1, 4), 3, Fraction{1}),
        };

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The ring passes the onset a beat in and then ends exactly on a slot that only holds
        // fingers. That slot draws no head, so the scan steps over it and the real onset three
        // beats in binds — three beats of clearance, so nothing trims.
        CHECK(presented[0] == Fraction{2});
        // Sharply discriminating: a held stop that bound would have trimmed the ring to 7/4.
        CHECK(presented[0] != Fraction{7, 4});
    }
}

// Rule 2 reaches a trimmed ring-through exactly as it reaches any other trimmed tail, because the
// trim is the same one call: nothing about passing an earlier onset changes what the payload floor
// or the gesture compression do at the end that binds.
TEST_CASE("Rule 2's floors reach a trimmed ring-through", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // Two beats of ring over an onset one beat in and another two beats in: the ring passes the
    // first and binds on the second, whose margin line sits at 7/4.
    std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{2}),
        note(at(1, 2), 2, Fraction{1}),
        note(at(1, 3), 3, Fraction{1}),
    };

    SECTION("a bend point past the margin floors the trim, and earlier points survive")
    {
        saved[0].keyframes = {
            Keyframe{.offset = Fraction{1}, .bend = 0.5},
            Keyframe{.offset = Fraction{15, 8}, .bend = 1.0},
        };

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The margin alone would stop at 7/4; the second point still says something new at 15/8,
        // so the tail runs to it and stops exactly there.
        CHECK(presented[0].sustain == Fraction{15, 8});
        // Both statements lie inside the kept ring, so the clip takes neither — a ring-through
        // that trims must not lose payload it still covers.
        CHECK(presented[0].keyframes.size() == 2);
    }

    SECTION("a glide arrival synthesized at the margin lands exactly on the new end")
    {
        // The import shape this fix was reported against: a tie merged across a neighbour,
        // carrying a shift-slide whose arrival keyframe the importer synthesizes at
        // `gap - margin`. The ring passes the neighbour and binds on the landing it reaches, and
        // the trim stops exactly on that arrival — which is the instant the arrival was placed
        // for. Presented whole, the tail ran a quarter beat past its own arrival and died on the
        // landing head with no gap at all.
        saved[0].keyframes = {
            Keyframe{.offset = Fraction{1}, .fret = 5},
            Keyframe{.offset = Fraction{7, 4}, .fret = 2},
        };

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
        // The pin keyframe restates the onset fret and so says nothing new, but it still lies
        // inside the kept ring, so the clip keeps it.
        CHECK(presented[0].keyframes.size() == 2);
    }

    SECTION("a slide-out rides the new end and keeps clear of the last stated fret")
    {
        saved[0].keyframes = {Keyframe{.offset = Fraction{1}, .fret = 7}};
        saved[0].slide_out = 9;

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // Bound once rather than indexed per assertion: each presented[0] is a separate
        // operator[] call, which the unchecked-optional-access analysis cannot tie back to
        // the guard, so the guard only reaches the access through a single name.
        const ChartNote& first = presented[0];
        REQUIRE(first.slide_out.has_value());
        // The glide at offset 1 is the last informative point, so the margin at 7/4 stands, and
        // the trail-off ends there — clear of that fret by a beat rather than sitting on it.
        CHECK(first.sustain == Fraction{7, 4});
        CHECK(first.keyframes.size() == 1);
    }
}

// Rule 3's earning input is all that survives of the exemption: a ring that passes an onset is
// still the statement it always was — a tie merged across a neighbour, a cross-voice hold — so it
// earns its group's tails even though the ring itself now trims.
TEST_CASE("A trimmed ring-through still earns its group's tails", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // RE-PINNED when the kept-sustain bound fell (user ruling 2026-09-07). The strum used to ring
    // seven eighths of a beat over onsets a half and a whole beat in; under the old bound that ring
    // earned nothing on its own, under the new one it earns outright, and the case would have
    // passed while proving nothing. The figure is restated a sixteenth-grid step tighter so the
    // earning input is the passed onset again: both members ring exactly ON the bound, which the
    // strict comparison drops.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{1, 2}),
        note(at(1, 1), 2, Fraction{1, 2}, 7),
        note(at(1, 1, Fraction{1, 4}), 3, Fraction{1}, 2),
        note(at(1, 1, Fraction{1, 2}), 4, Fraction{1}, 3),
    };

    const std::vector<Fraction> presented = presentedSustains(saved, map);
    REQUIRE(presented.size() == saved.size());
    // The ring passes the onset a quarter beat in and then binds on the onset half a beat in,
    // trimming to that onset's margin — the exemption would have presented the whole half beat.
    CHECK(presented[0] == Fraction{1, 4});
    CHECK(presented[0] != Fraction{1, 2});
    // Its partner rings the same length and passes the same onset, so neither member earns by its
    // own ring and the passed onset is the only thing that can earn this strum a tail. Without that
    // earning input both members would present nothing.
    CHECK(presented[1] == Fraction{1, 4});
    // The onset the ring passed is its own group and earns its own tail by running longer than the
    // bound; it passes the onset after it with nothing beyond to bind it.
    CHECK(presented[2] == Fraction{1});
    CHECK(presented[3] == Fraction{1});
}

// Rule 2: the margin yields to information and only as far as the information reaches. A point
// that CHANGES something floors the trim past the margin; trailing points that repeat what the
// tail already said hold nothing open and leave with the tail, clipped rather than rescaled.
TEST_CASE("Rule 2 floors the trim on informative payload and clips the rest", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // Two beats to the binding onset and a two-beat ring, so the ring does NOT pass the onset (that
    // would be a deliberate hold) and the margin alone would trim it to 7/4.
    std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{2}),
        note(at(1, 3), 2, Fraction{1}),
    };

    SECTION("a bend point that changes floors the trim past the margin")
    {
        saved[0].keyframes = {
            Keyframe{.offset = Fraction{1, 2}, .bend = 0.5},
            Keyframe{.offset = Fraction{15, 8}, .bend = 1.0},
        };

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The margin alone would have stopped at 7/4; the second bend point still says something
        // new at 15/8, so the tail runs to it and stops exactly there.
        CHECK(presented[0].sustain == Fraction{15, 8});
        CHECK(presented[0].keyframes.size() == 2);
    }

    SECTION("trailing points that repeat the curve leave with the tail")
    {
        saved[0].keyframes = {
            Keyframe{.offset = Fraction{1, 2}, .bend = 1.0},
            Keyframe{.offset = Fraction{15, 8}, .bend = 1.0},
            Keyframe{.offset = Fraction{2}, .bend = 1.0},
        };

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
        REQUIRE(presented[0].keyframes.size() == 1);
        // Clipped, never rescaled: the surviving statement keeps the offset the source authored.
        CHECK(presented[0].keyframes.front().offset == Fraction{1, 2});
        // The saved curve is untouched, so a later reveal can still draw the whole ring.
        CHECK(saved[0].keyframes.size() == 3);
    }

    SECTION("an equal-fret hold keyframe is a pin, not a change")
    {
        saved[0].keyframes = {
            Keyframe{.offset = Fraction{1, 2}, .fret = 7},
            Keyframe{.offset = Fraction{15, 8}, .fret = 7},
        };

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
        REQUIRE(presented[0].keyframes.size() == 1);
        CHECK(presented[0].keyframes.front().offset == Fraction{1, 2});
    }

    SECTION("a keyframe gliding to a new fret floors the trim like a bend does")
    {
        saved[0].keyframes = {
            Keyframe{.offset = Fraction{1, 2}, .fret = 7},
            Keyframe{.offset = Fraction{15, 8}, .fret = 9},
        };

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{15, 8});
        CHECK(presented[0].keyframes.size() == 2);
    }

    SECTION("a vibrato START floors the trim a minimum window PAST itself")
    {
        // The point-versus-interval distinction. A bend value and a fret are complete at the
        // instant they are reached, so the tail may stop exactly there; a shake is an interval
        // STATE, and a tail ending on its first instant would show it for no time at all and read
        // as no shake. So the information reaches one minimum slide window past the statement.
        saved[0].keyframes = {Keyframe{.offset = Fraction{7, 4}, .vibrato = VibratoState::Narrow}};

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // Stated exactly ON the margin line, so a point-shaped floor would leave the trim at 7/4;
        // the interval's extent is the whole of the difference.
        CHECK(presented[0].sustain == Fraction{7, 4} + g_minimum_slide_window);
    }

    SECTION("a vibrato END is a point, like every other statement")
    {
        // The interval before it already showed everything there was, so nothing is lost by
        // stopping exactly on the end — which is what makes the extra window above a property of
        // STARTS rather than of the vibrato channel.
        saved[0].vibrato = VibratoState::Narrow;
        saved[0].keyframes = {Keyframe{.offset = Fraction{7, 4}, .vibrato = VibratoState::Off}};

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
    }

    SECTION("a vibrato statement that repeats the standing state holds nothing open")
    {
        // Every channel is read against the value the note OPENS with, so a shake restated at an
        // instant it already had says nothing new and the margin trim stands.
        saved[0].vibrato = VibratoState::Narrow;
        saved[0].keyframes = {Keyframe{.offset = Fraction{7, 4}, .vibrato = VibratoState::Narrow}};

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
    }
}

// Rule 2's slide-out clause: an unpitched trail-off is gesture geometry rather than protected
// payload, so its presented terminal compresses back with the tail — but never below the minimum
// slide window, and never onto or before the last keyframe that survived the clip.
TEST_CASE("Rule 2 compresses a slide-out to the smallest legal end", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("the compressed end floors at the minimum slide window")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 4}),
            note(at(1, 1, Fraction{1, 4}), 2, Fraction{1}),
        };
        saved[0].slide_out = 8;

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The margin equals the whole gap, so the trim wants a zero-length tail; the gesture needs
        // somewhere to travel, so it compresses onto the window instead of vanishing.
        // Bound once rather than indexed per assertion: each presented[0] is a separate
        // operator[] call, which the unchecked-optional-access analysis cannot tie back to
        // the guard, so the guard only reaches the access through a single name.
        const ChartNote& first = presented[0];
        CHECK(first.sustain == g_minimum_slide_window);
        REQUIRE(first.slide_out.has_value());
        // The if-guard duplicates the REQUIRE on purpose: Catch2's macro is opaque to clang-tidy's
        // unchecked-optional-access analysis, and this is its canonical flow-visible form.
        if (first.slide_out.has_value())
        {
            CHECK(first.sustain == g_minimum_slide_window);
        }
    }

    SECTION("the compressed end is bumped strictly past the last surviving keyframe")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1, Fraction{1, 2}), 2, Fraction{1}),
        };
        saved[0].keyframes = {Keyframe{.offset = Fraction{1, 4}, .fret = 7}};
        saved[0].slide_out = 9;

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The margin line lands exactly on the surviving keyframe, so the trail-off takes the
        // first legal offset past it — one minimum window on — rather than sitting on it.
        // Bound once rather than indexed per assertion: each presented[0] is a separate
        // operator[] call, which the unchecked-optional-access analysis cannot tie back to
        // the guard, so the guard only reaches the access through a single name.
        const ChartNote& first = presented[0];
        CHECK(first.sustain == Fraction{3, 8});
        REQUIRE(first.slide_out.has_value());
        if (first.slide_out.has_value())
        {
            CHECK(first.sustain == Fraction{3, 8});
        }
        CHECK(first.keyframes.size() == 1);
    }
}

// Rule 2's scrape clause: a scrape's path is derived geometry, so the trim squishes the gesture
// rather than flooring the tail on it, and the leg rule decides how. Either way the presented
// terminal ends up exactly at the presented sustain, which is the shape the chart rules pin for a
// stored scrape.
TEST_CASE("Rule 2 compresses a scrape terminal by the leg rule", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a leg starting before the margin line ends on it")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}, 12),
            note(at(1, 3), 2, Fraction{1}),
        };
        saved[0].attack = NoteAttack::PickSlide;
        saved[0].slide_out = 3;

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // Bound once rather than indexed per assertion: each presented[0] is a separate
        // operator[] call, which the unchecked-optional-access analysis cannot tie back to
        // the guard, so the guard only reaches the access through a single name.
        const ChartNote& first = presented[0];
        REQUIRE(first.slide_out.has_value());
        // The single leg starts at the onset, so it has room to end on the margin line exactly and
        // gives up no spacing at all.
        // The terminal rides that end by construction now: a slide-out stores no offset of its
        // own, so the presented sustain IS where the gesture stops.
        CHECK(first.sustain == Fraction{7, 4});
    }

    SECTION("a leg starting inside the margin halves its distance to the onset")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}, 12),
            note(at(1, 3), 2, Fraction{1}),
        };
        saved[0].attack = NoteAttack::PickSlide;
        saved[0].keyframes = {Keyframe{.offset = Fraction{15, 8}, .fret = 7}};
        saved[0].slide_out = 3;

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        REQUIRE(presented.size() == saved.size());
        // Bound once rather than indexed per assertion: each presented[0] is a separate
        // operator[] call, which the unchecked-optional-access analysis cannot tie back to
        // the guard, so the guard only reaches the access through a single name.
        const ChartNote& first = presented[0];
        REQUIRE(first.slide_out.has_value());
        // The turnaround at 15/8 is already past the margin line at 7/4, so the last leg cannot
        // yield the margin and splits the remaining distance to the onset instead.
        CHECK(first.sustain == Fraction{31, 16});
        // Inside the margin by design — the sanctioned exception — but still strictly before the
        // onset it is crowded against, which is what the halving always guarantees.
        CHECK(first.sustain > Fraction{7, 4});
        CHECK(first.sustain < Fraction{2});
    }
}

// Rule 3 is the only rule with a group verdict: every string of a chord rings from one stroke, so
// a tail any member earns keeps every member's, and a group where nobody earns one presents none.
// A tail is earned by a sustain technique, by a deliberate hold, or by an ACTUAL ring running
// LONGER than the kept-sustain bound — the actual ring, never the trimmed one, because the trim is
// presentation and the question is what the chart states.
TEST_CASE("Rule 3 decides one tail verdict for a whole onset group", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{1, 2}),
        note(at(1, 1), 2, Fraction{1, 2}),
        note(at(1, 2), 1, Fraction{1, 2}),
        note(at(1, 2), 2, Fraction{1, 2}),
        note(at(1, 3), 1, Fraction{3, 4}),
        note(at(1, 3), 2, Fraction{1, 2}),
        note(at(2, 1), 1, Fraction{3}),
        note(at(2, 1), 2, Fraction{1, 2}),
        note(at(2, 2), 3, Fraction{1}),
    };
    // One member's vibrato is a whole-note technique that changes no payload offset, so it can only
    // be the group verdict talking when its effect-free partner keeps a tail too.
    saved[1].vibrato = VibratoState::Narrow;

    const std::vector<Fraction> presented = presentedSustains(saved, map);
    REQUIRE(presented.size() == saved.size());
    // A technique on one member keeps every member's tail.
    CHECK(presented[0] == Fraction{1, 2});
    CHECK(presented[1] == Fraction{1, 2});
    // The identical group with no technique anywhere and no member running past the bound is a
    // chug, not a sustain, and presents no tail at all. Both members ring exactly ON the bound,
    // which rule 3's strict comparison drops.
    CHECK(presented[2] == Fraction{});
    CHECK(presented[3] == Fraction{});
    // One member notated the shortest step past the bound — re-pinned from a whole beat when the
    // bound fell (user ruling 2026-09-07) — earns the group's tails, and its on-the-bound partner
    // keeps its own.
    CHECK(presented[4] == Fraction{3, 4});
    CHECK(presented[5] == Fraction{1, 2});
    // A deliberate hold earns them as well, and keeps its own ring whole.
    CHECK(presented[6] == Fraction{3});
    CHECK(presented[7] == Fraction{1, 2});
    CHECK(presented[8] == Fraction{1});
}

// THE BOUND ITSELF (user ruling 2026-09-07): a ring earns a drawn tail by running LONGER than the
// kept-sustain bound, and the comparison is STRICT because that is the rule as worded. The cases
// around this one read the bound through figures and never name its value; this is the one case
// that exercises the value directly, so `g_minimum_kept_sustain_whole_note` has exactly one place
// to answer to when it moves — which it will, since the bound is headed for a user-tunable option.
// It fell here from a longer value the day the 3D board's curtain began resting every
// technique-free tail: with the ribbon no longer duplicating the rhythm at distance, a shorter ring
// can afford to draw one.
TEST_CASE("Rule 3 earns a tail only for a ring past the kept-sustain bound", "[core][chart]")
{
    SECTION("in 4/4 an exact eighth drops its tail and a dotted eighth keeps one")
    {
        const TempoMap map = fourFourMap();
        // Half a beat in 4/4 IS an eighth note: it lands exactly ON the bound, and the strict
        // comparison is the whole of why it presents nothing.
        CHECK(loneTail(note(at(1, 1), 1, Fraction{1, 2}), map) == Fraction{});
        // Three quarters of a beat is a dotted eighth — the shortest notated value past the bound
        // — and it presents its ring whole, nothing standing after it to trim against.
        CHECK(loneTail(note(at(1, 1), 1, Fraction{3, 4}), map) == Fraction{3, 4});
        // A dotted sixteenth is under the bound rather than on it, and drops for the ordinary
        // reason: the strictness above is the only thing the eighth needed.
        CHECK(loneTail(note(at(1, 1), 1, Fraction{3, 8}), map) == Fraction{});
    }

    SECTION("one dotted-eighth member keeps every member's tail")
    {
        const TempoMap map = fourFourMap();
        // Rule 3's atom is the onset group, so the earning member carries its exact-eighth
        // stackmate: the verdict is the strum's, never the string's, and each keeps its own length.
        const std::vector<Fraction> earned = presentedSustains(
            {note(at(1, 1), 1, Fraction{3, 4}), note(at(1, 1), 2, Fraction{1, 2}, 7)}, map);
        REQUIRE(earned.size() == 2);
        CHECK(earned[0] == Fraction{3, 4});
        CHECK(earned[1] == Fraction{1, 2});
        // Shorten the one member that earns to an exact eighth and the whole strum goes tail-less,
        // which is what makes the pair above a group verdict rather than two independent ones.
        const std::vector<Fraction> unearned = presentedSustains(
            {note(at(1, 1), 1, Fraction{1, 2}), note(at(1, 1), 2, Fraction{1, 2}, 7)}, map);
        REQUIRE(unearned.size() == 2);
        CHECK(unearned[0] == Fraction{});
        CHECK(unearned[1] == Fraction{});
    }

    SECTION("in 12/8 one signature beat is the eighth that drops")
    {
        const TempoMap map = twelveEightMap();
        // THE POINT OF REFERENCING A NOTE VALUE and never a beat: a 12/8 beat IS an eighth note,
        // so it sits exactly ON the bound and drops, where a one-BEAT bound would hand nearly
        // every note of a 12/8 song a tail.
        CHECK(loneTail(note(at(1, 1), 1, Fraction{1}), map) == Fraction{});
        // And the dotted eighth keeps its tail here exactly as it does in 4/4 — one and a half
        // beats in this meter, the same note value either way.
        CHECK(loneTail(note(at(1, 1), 1, Fraction{3, 2}), map) == Fraction{3, 2});
    }
}

// Rules 2 and 3 meet on one strum: the keep-or-drop verdict is the GROUP's, the length is each
// string's own. Without this pairing a double stop whose bent string earns the tail would either
// show a lone tail beside an unsounded-looking partner (verdict per string) or stretch the
// partner's tail to the bent one's length (length shared). Ported from the import policy's own
// suite, which is where it was pinned before the rules moved here.
TEST_CASE("A group shares its tail verdict but not its tail lengths", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{1, 2}),
        note(at(1, 1), 2, Fraction{1, 2}, 7),
        note(at(1, 1, Fraction{1, 2}), 3, Fraction{1, 2}, 2),
    };
    // A curve that keeps rising to the ring's end: its last CHANGE is the final point, so rule 2
    // floors the trim there.
    saved[0].keyframes = {Keyframe{.offset = Fraction{1, 2}, .bend = 2.0}};

    const std::vector<Fraction> presented = presentedSustains(saved, map);
    REQUIRE(presented.size() == saved.size());
    // Half a beat sits exactly ON the kept-sustain bound and is dropped by rule 3's strict
    // comparison, so only the bend earns this strum its tails — and it earns them for the plain
    // partner too.
    CHECK(presented[0] == Fraction{1, 2});
    // The partner's own length is the margin before the next onset, not the bent string's.
    CHECK(presented[1] == Fraction{1, 4});
}

// Rule 4 (E25) reads the note as the earlier rules leave it: a dead string rings nothing, so a
// plain tail on one is silence pretending to be sound — while tremolo (a chug) or a slide payload
// (a dragged mute) keeps it making noise or travelling and keeps its tail.
TEST_CASE("Rule 4 presents no tail on a dead note that makes no noise", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{2}),
        note(at(2, 1), 1, Fraction{2}),
        note(at(3, 1), 1, Fraction{2}),
        note(at(4, 1), 1, Fraction{2}),
    };
    for (ChartNote& dead_note : saved)
    {
        dead_note.dead = true;
    }
    saved[1].tremolo = true;
    saved[2].keyframes = {Keyframe{.offset = Fraction{1}, .fret = 7}};
    saved[3].slide_out = 8;

    const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
    REQUIRE(presented.size() == saved.size());
    CHECK(presented[0].sustain == Fraction{});
    CHECK(presented[1].sustain == Fraction{2});
    CHECK(presented[2].sustain == Fraction{2});
    CHECK(presented[3].sustain == Fraction{2});
    // The stored ring survives the rule, which is what a legato claim after a muted cluck reads:
    // pinning a dead note at zero is the regression this model exists to avoid.
    CHECK(saved[0].sustain == Fraction{2});
}

// The numbers the Guitar Pro import suite pins, reproduced from hand-built saved streams so the
// presentation rules and the import policy they were lifted from can be compared directly.
TEST_CASE("Presented tails reproduce the import policy's pinned trims", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a tie-merged chord trimmed at a changed onset presents 7/4")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 1), 2, Fraction{2}, 7),
            note(at(1, 3), 1, Fraction{2}, 3),
            note(at(1, 3), 2, Fraction{2}),
        };

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0] == Fraction{7, 4});
        CHECK(presented[1] == Fraction{7, 4});
    }

    SECTION("a predecessor bound by a grace's sounding onset presents 5/8")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{7, 8}),
            note(at(1, 1, Fraction{7, 8}), 2, Fraction{1, 8}, 7),
            note(at(1, 2), 2, Fraction{1}, 8),
        };
        // Vibrato only earns the group's tail (rule 3); it changes no payload offset, so the 5/8 is
        // rule 1's arithmetic alone: 7/8 to the ornament's sounding onset, less the quarter-beat
        // margin.
        saved[0].vibrato = VibratoState::Narrow;

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0] == Fraction{5, 8});
        // The ornament's own lead-length tail does not survive: its principal an eighth later binds
        // it, and the margin is wider than the gap.
        CHECK(presented[1] == Fraction{});
        CHECK(presented[2] == Fraction{1});
    }

    SECTION("the same geometry with a longer ring passes the ornament and binds on its principal")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1}),
            note(at(1, 1, Fraction{7, 8}), 2, Fraction{1, 8}, 7),
            note(at(1, 2), 2, Fraction{1}, 8),
        };
        saved[0].vibrato = VibratoState::Narrow;

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        // A full beat runs strictly past the ornament's sounding onset at 7/8, so that onset does
        // not bind it; the principal a beat in does, and the trim is that onset's margin. Binding
        // on the SOUNDING position is what the model buys: the import policy read a separately
        // notated beat instead and trimmed this ring to 5/8, the answer the section above pins.
        CHECK(presented[0] == Fraction{3, 4});
        CHECK(presented[0] != Fraction{5, 8});
    }
}

// The span-implied hold: a chug under a hand shape presents no tail at all, yet the shape is what
// tells the player to keep holding it — so the SPAN is the whole answer and each member's own ring
// does not cut it short (user ruling 2026-08-29). This is the repeat-chain shape: three identical
// strums under one span, which is exactly the arrangement the board draws as one chord box
// followed by repeat boxes. Capping at the ring ended the first strum's hold at the second strum's
// onset, and the boxes that follow draw no heads of their own, so the pinned shape vanished one
// box into the chain.
TEST_CASE("A span holds a restruck chug for the whole span", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{1, 2}),
        note(at(1, 1), 2, Fraction{1, 2}, 7),
        note(at(1, 2), 1, Fraction{1, 2}),
        note(at(1, 2), 2, Fraction{1, 2}, 7),
        note(at(1, 3), 1, Fraction{1, 2}),
        note(at(1, 3), 2, Fraction{1, 2}, 7),
    };
    // The span ends a quarter beat into the third strum, so every strum's hold is its own distance
    // to that one end.
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{9, 4}},
    };

    const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
    REQUIRE(holds.size() == saved.size());
    // Every chug is a half-beat effect-free ring, so rule 3 presents no tail on any of them — and
    // the tail law never sees one, because it skips a tail that is already empty.
    for (const Fraction shown : presentedSustains(saved, map))
    {
        CHECK(shown == Fraction{});
    }
    // The first strum is held for the whole span — past its own half-beat ring and past the two
    // restrikes that cut it, because a restrike stops the string without releasing the shape.
    CHECK(holds[0] == Fraction{9, 4});
    CHECK(holds[1] == Fraction{9, 4});
    // And the hold ends at the LAST restatement's end, not at the first's: every strum in the
    // chain reaches the same span end, so each one's remainder is shorter than the last's.
    CHECK(holds[2] == Fraction{5, 4});
    CHECK(holds[3] == Fraction{5, 4});
    CHECK(holds[4] == Fraction{1, 4});
    CHECK(holds[5] == Fraction{1, 4});
}

// The two cases the span-implied hold deliberately does NOT claim: drawn tails and chokes. The
// third case that stood here — "a single note is no strum" — was deleted by the one-rule collapse
// (user sighting 2026-09-03): a lone covered member is a grip member exactly as a strummed one is.
TEST_CASE("Presented tails and dead groups hold what they show", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a resting spill holds its own stored ring past the reach")
    {
        // The first member LEAVES the span; since the spill amendment the stroke rests anyway,
        // and the hold channel answers honestly on both sides of the reach: the spilling ring
        // exceeds the span's four beats and keeps its own 9/2 (the string genuinely rings
        // there), while its short partner is pinned to the reach — the tenure the grip states.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{9, 2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
            note(at(2, 1), 3, Fraction{1}, 3),
        };

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        CHECK(holds[0] == Fraction{9, 2});
        CHECK(holds[1] == Fraction{4});
        CHECK(holds[2] == Fraction{1});
    }

    SECTION("an all-dead group is choked rather than held")
    {
        // No unanimity rule states this any more: every member is skipped on its own account for
        // being dead, so a group where they all are chokes by the very line that chokes one of
        // them inside a live strum.
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
            note(at(2, 1), 3, Fraction{1}, 3),
        };
        saved[0].dead = true;
        saved[1].dead = true;

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        CHECK(holds[0] == Fraction{});
        CHECK(holds[1] == Fraction{});
        CHECK(holds[2] == Fraction{1});
    }

    SECTION("a lone covered chug is held to the reach like a strummed one")
    {
        // RULED INVERSION (user sighting 2026-09-03, the one-rule collapse): this section pinned
        // "a single note is no strum and holds only its own tail". The strum-size gate is deleted
        // — a lone covered tail-less member is a grip member, and in a derived chart a span
        // reaching past its ring proves the renewal (an un-renewed death breaks the grip), so the
        // board pins the finger for the whole tenure here exactly as it does for a pair.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(2, 1), 3, Fraction{1}, 3),
        };

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        CHECK(holds[0] == Fraction{4});
        // The seam note stands exactly where the span's reach ends, so the extension adds nothing
        // and it states its own hold — the in-case control that the extension takes only what a
        // reach actually covers.
        CHECK(holds[1] == Fraction{1});
    }
}

// A RIGHT-HAND ONSET is a member of nothing the grip states, so it never inherits the span's
// reach — the same scope the tail law takes on both of its sides. The strum COUNT it used to
// pollute is deleted with the strum-size gate (the 2026-09-03 one-rule collapse), so the one live
// correction left is the inheritance: extending a tap pinned a head the fretting hand never put
// down.
TEST_CASE("A tap is no part of the strum a span holds", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // A tap on its own string, ringing the same effect-free half beat every chug member here does,
    // so nothing below can turn on a length instead of on the hand that made the onset.
    const auto tap = [](const GridPosition position, const int string) {
        ChartNote tapped = note(position, string, Fraction{1, 2}, 9);
        tapped.attack = NoteAttack::Tap;
        return tapped;
    };

    SECTION("a fretted note beside a tap extends alone, and the tap inherits nothing")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            tap(at(1, 1), 3),
            note(at(1, 2), 1, Fraction{1, 2}),
            note(at(1, 2), 2, Fraction{1, 2}, 7),
        };

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        // Every ring here is an effect-free half beat, so rule 3 empties all four tails and the
        // span rule is the only thing left that can answer.
        for (const Fraction shown : presentedSustains(saved, map))
        {
            CHECK(shown == Fraction{});
        }
        // The lone fretted member extends on its own account now (the one-rule collapse) — four
        // beats from its onset to the span's close — while the tap beside it inherits nothing.
        CHECK(holds[0] == Fraction{4});
        CHECK(holds[1] == Fraction{});
        // The pair at beat two extends the same way, three beats to the same close: one rule, no
        // strum count for the tap to pollute.
        CHECK(holds[2] == Fraction{3});
        CHECK(holds[3] == Fraction{3});
    }

    SECTION("a tap inside a real strum pins its partners and never its own head")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
            tap(at(1, 1), 4),
        };

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        // Two fretted strings sound together, so the strum is real and the span holds both of them
        // for all four beats of it.
        CHECK(holds[0] == Fraction{4});
        CHECK(holds[1] == Fraction{4});
        // The tap rides the same stroke and still holds only what it draws.
        CHECK(holds[2] == Fraction{});
    }
}

// A DEAD member of a live strum is CHOKED, never held. Rule 4 empties a dead note's plain tail, so
// an emptiness gate alone would hand the span's whole reach to a percussive choke — the one hold
// in the chart that would say the finger stayed down where the chart says the string was killed.
// The strum COUNT the dead string used to feed is gone with the strum-size gate; the dead skip is
// what remains, per member, and its live partner is what the span pins.
TEST_CASE("A dead member of a live strum is choked while its partners pin", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a dead string inside a live chug keeps its own end")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
            note(at(1, 1), 3, Fraction{1, 2}, 9),
        };
        saved[2].dead = true;

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        CHECK(holds[0] == Fraction{4});
        CHECK(holds[1] == Fraction{4});
        CHECK(holds[2] == Fraction{});
    }

    SECTION("a dead-and-live dyad pins the live string and chokes the dead one")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
        };
        saved[1].dead = true;

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        // Two strings under one stroke, one of them killed: the live string is held for the
        // shape on its own account (no strum count exists to argue about), and the choke is
        // never pinned.
        CHECK(holds[0] == Fraction{4});
        CHECK(holds[1] == Fraction{});
    }

    SECTION("a dead member whose partner keeps its ribbon is choked by rule 4 alone")
    {
        // A live ring OUTLIVING the span (so the tail law takes nothing and the ribbon genuinely
        // draws), beside a dead one whose only zero is the one rule 4 makes. This is the lie in
        // its purest form: the dead member's emptiness has nothing to do with the kept-sustain
        // bound, and handing it the span's reach would pin a choke as a held finger.
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{9, 2}),
            note(at(1, 1), 2, Fraction{2}, 7),
        };
        saved[1].dead = true;

        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(holds.size() == 2);
        CHECK(shown[0] == Fraction{9, 2});
        CHECK(shown[1] == Fraction{});
        // The live member draws its own ribbon and holds exactly that; the dead one holds nothing,
        // where the span's four beats would otherwise have been handed to it.
        CHECK(holds[0] == Fraction{9, 2});
        CHECK(holds[1] == Fraction{});
    }
}

// Which span a strum inherits from, and for how long the span machinery keeps looking. Nothing
// forbids spans from overlapping, and a shape that began earlier and runs longer holds the same
// strum just as well — but a single cursor remembering the latest STARTING span let a short one
// beginning inside a long one shadow it, so the strum read as released and every hammer-on or
// pull-off it justified was repaired away. Every note here rings half a beat: effect-free and
// exactly ON the kept-sustain bound, which rule 3's strict comparison drops, so no tail is
// presented and the span rule is what answers.
TEST_CASE("A strum's inherited hold comes from the furthest-reaching span", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("an earlier, longer span is not shadowed by a later, shorter one")
    {
        // Span A covers global beats 0 through 8.25; span B starts later (beat 4) and ends at 5,
        // long before the chord at measure 3 beat 1 (global beat 8).
        const std::vector<ChartShape> shapes = {
            ChartShape{.position = at(1, 1), .sustain = Fraction{33, 4}},
            ChartShape{.position = at(2, 1), .sustain = Fraction{1}},
        };
        const std::vector<ChartNote> saved = {
            note(at(3, 1), 1, Fraction{1, 2}),
            note(at(3, 1), 2, Fraction{1, 2}, 7),
        };

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == 2);
        // A quarter beat of span A is left, and that remainder is the hold. A cursor that
        // remembered only span B would find no cover at all and hold nothing.
        CHECK(holds[0] == Fraction{1, 4});
        CHECK(holds[1] == Fraction{1, 4});

        // Listing order must not matter either: the same two spans the other way round give the
        // same answer, which a last-writer-wins cursor could not promise.
        const std::vector<ChartShape> reversed = {shapes[1], shapes[0]};
        const std::vector<Fraction> held_reversed = holdsUnderSpans(saved, reversed, map);
        REQUIRE(held_reversed.size() == 2);
        CHECK(held_reversed[0] == Fraction{1, 4});
    }

    SECTION("the cursor advances to a later span, and past every span nothing extends")
    {
        // One span over measure 1, another over measure 4; measure 5 is past both.
        const std::vector<ChartShape> shapes = {
            ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
            ChartShape{.position = at(4, 1), .sustain = Fraction{2}},
        };
        const std::vector<ChartNote> saved = {
            note(at(4, 1), 1, Fraction{1, 2}),
            note(at(4, 1), 2, Fraction{1, 2}, 7),
            note(at(5, 1), 1, Fraction{1, 2}),
            note(at(5, 1), 2, Fraction{1, 2}, 7),
        };

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        // The second span reaches two beats past the strum, and the span is the whole answer, so
        // each member is held for all of it. (A derived span never outruns its members' rings
        // this way without a restatement to carry it — these shapes are authored straight into
        // the call, which is what lets the section isolate the cursor.)
        CHECK(holds[0] == Fraction{2});
        CHECK(holds[1] == Fraction{2});
        // No span covers measure 5, so its strum holds exactly what it presents: nothing.
        CHECK(holds[2] == Fraction{});
        CHECK(holds[3] == Fraction{});
    }
}

// A silently-held stop is not there as far as SOUND is concerned, and three rules that walk the
// note stream have to read it that way. Each of these had no case before the substrate swap put
// held stops in the stream, and each fails loudly if its skip is removed.
TEST_CASE("Presentation and its bounds read past a silently-held stop", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a held stop is not a binding onset, so the tail in front of it keeps its length")
    {
        // Rule 1 trims a ring to clear the HEAD that follows it, and a held stop draws none. The
        // note rings two beats; a hold lands ONE beat in and a real onset TWO beats in, so the
        // trim that fires is the real onset's margin and the hold changes nothing.
        const std::vector<ChartNote> with_hold{
            note(at(1, 1), 1, Fraction{2}),
            heldStop(at(1, 2), 3, 7),
            note(at(1, 3), 2, Fraction{1}),
        };
        std::vector<ChartNote> without_hold = with_hold;
        without_hold.erase(without_hold.begin() + 1);
        CHECK(presentedSustains(with_hold, map)[0] == presentedSustains(without_hold, map)[0]);
        // And the case discriminates sharply: two beats less the 4/4 margin, where a hold that
        // bound would have left one beat less that margin instead.
        CHECK(presentedSustains(with_hold, map)[0] == Fraction{2} - minimumSustainDistanceBeats(4));
        CHECK(presentedSustains(with_hold, map)[0] != Fraction{1} - minimumSustainDistanceBeats(4));
    }

    SECTION("a held stop on the string bounds no ring")
    {
        // 40-Q2-B bounds a ring at the next STRIKE on its own string, and nothing struck silently
        // stops a string that is already sounding.
        std::vector<ChartNote> notes{
            note(at(1, 1), 1, Fraction{4}),
            heldStop(at(1, 2), 1, 7),
        };
        CHECK_FALSE(sustainBoundOf(notes, notes[0], map).has_value());
        normalizeSustainOverlaps(notes, map);
        CHECK(notes[0].sustain == Fraction{4});
    }
}

// THE TAIL LAW (rule 5 of presentedChartNotes, the one authority; THE CURTAIN IS UNIVERSAL, user
// ruling 2026-09-07): a tail that shows no technique information RESTS, and the law PUBLISHES the
// verdict while EMPTYING NOTHING. Each case below pins its resting tails at the bare rules-1-to-4
// form, and reads verdicts off the published offsets rather than off emptied tails.
//
// NO CASE HERE STATES A SPAN any more, and that absence is the ruling: coverage left the law
// outright, so the fixtures that existed to vary the span partition, the posture or the ring's
// relation to a close are DELETED rather than re-pinned — a chart with no furniture at all now
// rests exactly the tails a bracketed one does. `holdsUnderSpans` is where a span still means
// something, and the hold cases below are the ones that name one.
//
// The conjunct history (TIME narrowed to the own-onset span; STRING and END died as proofs;
// CROSSING deleted by ruling 2026-09-04; CONTAINMENT deleted by the spill amendment; the whole
// coverage question deleted 2026-09-07) lives with the law, not here.

// THE UNIVERSAL CURTAIN (user ruling 2026-09-07: "we should just try applying the curtain
// universally to all tails that don't show technique information"). The headline case, and the one
// that would have been unwritable the day before: every fixture here is judged with NO furniture in
// the chart at all, and the verdicts are the ones a bracketed figure gets. The never-rests
// disjunction is the whole of what withholds one now.
//
// THE HOLD is pinned in the same case because the two decisions were taken together. `chartHolds`
// used to floor a RESTING member at its STORED ring; under the universal curtain that would run
// every lone note's head pin out to its untrimmed ring and into the next note's margin, so the
// floor is keyed on SPAN COVERAGE instead — a covered member raises to its stored ring and then to
// the span's reach exactly as before, and a lone one holds the tail it presents.
TEST_CASE("Every plain tail rests, span or no span", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a lone plain note rests from its head and holds only what it presents")
    {
        // A four-beat ring at the top of the piece, a filler a measure later that binds its trim,
        // and a covered member two measures on. The first two stand under nothing at all.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(2, 1), 2, Fraction{1}, 7),
            note(at(3, 1), 3, Fraction{1}, 9),
        };
        const std::vector<ChartShape> shapes = {
            ChartShape{.position = at(3, 1), .sustain = Fraction{4}},
        };

        const std::vector<bool> hidden = hiddenOf(saved, map);
        const std::vector<std::optional<Fraction>> rests = restedOffsetsOf(saved, map);
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);

        REQUIRE(hidden.size() == 3);
        REQUIRE(rests.size() == 3);
        REQUIRE(shown.size() == 3);
        REQUIRE(holds.size() == 3);
        // THE RULING: no span stands over this ring anywhere along it, and it rests from its head
        // all the same. Before 2026-09-07 no verdict was passed on it at all.
        CHECK(hidden[0]);
        CHECK(rests[0] == std::optional{Fraction{}});
        // The 2D-facing length is untouched by any of it: rule 1's margin trim against the filler
        // a measure on, and nothing else.
        CHECK(shown[0] == Fraction{15, 4});
        // THE HOLD DECISION, and the two candidate answers sit strictly apart: the presented 15/4
        // is what a lone resting note holds, NOT the stored four beats the old resting floor would
        // have handed it — which would have pinned the head a quarter beat into the filler's own
        // margin.
        CHECK(holds[0] == Fraction{15, 4});
        CHECK(holds[0] != Fraction{4});
        // And the covered member beside them still reaches its span: the tenure floor moved, it did
        // not go. Its own ring is one beat and the span reaches four.
        CHECK(hidden[2]);
        CHECK(holds[2] == Fraction{4});
    }

    SECTION("a bend released mid-ring rests from the release, with no span anywhere")
    {
        // The finished-statement split, judged on open board: the channel goes plain at the
        // release, so the stated portion stays always visible and the remainder rests from there.
        ChartNote released = note(at(1, 1), 1, Fraction{4});
        released.bend = 2.0;
        released.keyframes = {Keyframe{.offset = Fraction{2}, .bend = 0.0}};
        const std::vector<ChartNote> saved = {std::move(released)};

        const std::vector<std::optional<Fraction>> rests = restedOffsetsOf(saved, map);
        REQUIRE(rests.size() == 1);
        CHECK(rests[0] == std::optional{Fraction{2}});
        CHECK(presentedSustains(saved, map)[0] == Fraction{4});
    }

    SECTION("a bend held to the ring's end never rests, span or no span")
    {
        // The one thing the universal curtain still withholds a verdict from — and the control
        // that keeps the section above from passing by the law simply marking everything.
        ChartNote bent = note(at(1, 1), 1, Fraction{4});
        bent.bend = 2.0;
        const std::vector<ChartNote> saved = {std::move(bent)};

        const std::vector<std::optional<Fraction>> rests = restedOffsetsOf(saved, map);
        REQUIRE(rests.size() == 1);
        CHECK_FALSE(rests[0].has_value());
        CHECK(presentedSustains(saved, map)[0] == Fraction{4});
    }

    SECTION("a handover rests with an EMPTY remainder, so nothing of it is curtained")
    {
        // The transfer finishes at the takeover, so the landmark is the ribbon's own end: the
        // member rests, and \ref hasRestingRemainder — the reading both distance-scoped consumers
        // share — says the curtain owns none of it.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}, 5),
            connected(at(1, 3), 1, Fraction{1}, 3),
        };

        const std::vector<ChartNote> presented = presentedNotesOf(saved, map);
        const std::vector<std::optional<Fraction>> rests = restedOffsetsOf(saved, map);
        REQUIRE(presented.size() == 2);
        REQUIRE(rests.size() == 2);
        // Rule 1 binds the source at its successor's head, a margin short; the landmark is that
        // very end.
        CHECK(presented[0].sustain == Fraction{7, 4});
        CHECK(rests[0] == std::optional{Fraction{7, 4}});
        CHECK_FALSE(hasRestingRemainder(rests[0], presented[0]));
    }
}

TEST_CASE("A co-terminating let-ring figure rests ribbonless", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a picked let-ring arpeggio goes ribbonless, closer included")
    {
        // Three let-ring plucks, one per beat on three strings, every ring notated to the figure's
        // own close at beat five — the honest let-ring texture: rings crossing each other and
        // ending together where the statement ends.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 2), 2, Fraction{3}, 7),
            note(at(1, 3), 3, Fraction{2}, 9),
        };

        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);

        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // All three rings are plain to their ends, so all three rest and the rails — where a figure
        // has any — are the whole statement. The SPAN this case used to state is gone with the
        // universal curtain (2026-09-07): it was never what made a plain ribbon uninformative.
        //
        // THE CLOSER IS NO LONGER SPECIAL, and this verdict is the reversal itself. The 2026-09-01
        // fixture freed this third ring through the CROSSING conjunct — nothing sounds inside it,
        // so the figure never demonstrably continued there. The user deleted that conjunct on
        // 2026-09-04 ("the last note in the span shouldn't get treated special... hide ALL tails
        // except the explicit exceptions"), and this is what the deletion buys: a co-terminating
        // let-ring figure rests ribbonless end to end, with the rails, the repeat boxes and the 3D
        // hold-pinning stating the tenure and Alt or the caret revealing the close.
        CHECK(hidden[0]);
        CHECK(hidden[1]);
        CHECK(hidden[2]);
        // THE EXECUTION-FORM AMENDMENT (user ruling 2026-09-03): the verdict no longer empties the
        // tail. Hidden means the BOARD rests it — suppressed at distance, revealed as the head
        // approaches the hit line — while the presented value keeps its rules-1-to-4 form. Rule 1
        // leaves all three whole, because each passes the onsets after it.
        CHECK(shown == std::vector<Fraction>{Fraction{4}, Fraction{3}, Fraction{2}});
    }

    SECTION("a long hold and the ring that ends with it are hidden alike")
    {
        // A four-beat ring with one strum inside it, and a second ring struck at beat three that
        // ends with it and has nothing after it. Under the deleted crossing conjunct the second
        // one drew; the two have been one sentence since 2026-09-04.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 3), 2, Fraction{2}, 7),
        };

        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);

        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        CHECK(hidden[1]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value here is the rules-1-to-4 form, which rule 1 leaves whole on
        // both members (each passes every onset after it).
        CHECK(shown == std::vector<Fraction>{Fraction{4}, Fraction{2}});
    }
}

// THE MOTIVATING ODDITY, REVERSED (user, 2026-09-01 then 2026-09-04). The 2026-09-01 report was
// that the LAST member of a bracketed figure held a long ring and showed no tail whatever; the fix
// of the day freed it through the CROSSING conjunct — nothing sounds inside the closer's ring, so
// the figure never demonstrably continued there — and this case pinned the whole tail. On
// 2026-09-04 the user reversed that ruling outright ("the last note in the span shouldn't get
// treated special... hide ALL tails except the explicit exceptions"), deleted the conjunct with
// it, and priced the consequence: the closer rests ribbonless like every other member, and the box
// rails, the repeat boxes and the 3D hold-pinning are what state the tenure. The fixture is kept
// and INVERTED so the reversal stays on the record rather than vanishing with the assertion.
//
// Since the execution-form amendment (user ruling 2026-09-03) the reversal lives in the VERDICT
// alone: the law no longer empties the tail it hides, so both members present their rules-1-to-4
// rings and only `hidden` says which one the board rests. The span the fixture used to state went
// with the universal curtain (2026-09-07); what survives is the pair of unequal rings, which is
// the shape the reversal was reported against.
TEST_CASE("A long hold rests like every other member", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // The second member is struck on beat three and rings two beats; the first outlives it by half
    // a beat. Neither states anything of its own, so both rest — the reading LEAVING once escaped
    // (the 2026-09-06 spill amendment), and now escapes for no one because there is no span left
    // to leave.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{9, 2}),
        note(at(1, 3), 2, Fraction{2}, 7),
    };

    const std::vector<Fraction> shown = presentedSustains(saved, map);
    const std::vector<bool> hidden = hiddenOf(saved, map);

    REQUIRE(shown.size() == 2);
    REQUIRE(hidden.size() == 2);
    CHECK(hidden[0]);
    CHECK(hidden[1]);
    // Both present their rules-1-to-4 rings: the verdict marks, it never empties. Rule 1 binds
    // neither — each passes every onset after it — so the pair is the notated pair exactly.
    CHECK(shown[0] == Fraction{9, 2});
    CHECK(shown[1] == Fraction{2});
}

// CONJUNCT 4 — CROSSING — WAS DELETED BY RULING (user, 2026-09-04), and this fixture is its grave
// marker. The conjunct asked whether the span demonstrably CONTINUED inside the ring — some later
// fretting-hand sounding head standing VISIBLY within it, measured with rule 1's comparison and
// the drawn margin as clearance — and it is what freed the closer, the plain strum under a chord
// box and the slow restrike chain, three figures by scope rather than by three rulings. The user
// reversed it whole: "the last note in the span shouldn't get treated special... hide ALL tails
// except the explicit exceptions." `ringPassesHead` lost its clearance parameter with it, since
// rule 1 was always the caller that asked for none.
//
// The old discriminating pair is kept, and both halves now answer the same way: where a head sits
// inside the ring, and whether it clears the margin, buys the tail nothing.
TEST_CASE("A head inside the ring is no part of the verdict", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // A four-beat ring, and one head inside it whose distance from the ring's end is the only
    // thing that moves. The span this pair used to stand under went with the universal curtain
    // (2026-09-07); the conjunct it buried is what the pair still records.
    const auto figure = [](const GridPosition head) {
        return std::vector<ChartNote>{
            note(at(1, 1), 1, Fraction{4}),
            note(head, 2, Fraction{1, 4}, 7),
        };
    };

    SECTION("a head the ring clears by more than the margin is hidden")
    {
        const std::vector<ChartNote> saved = figure(at(1, 4, Fraction{1, 2}));
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — the whole four beats, since
        // the ring passes that inner head by half a beat and nothing binds it after.
        CHECK(shown[0] == Fraction{4});
    }

    SECTION("a head the ring clears by exactly the margin is hidden too")
    {
        const std::vector<ChartNote> saved = figure(at(1, 4, Fraction{3, 4}));
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        // Rested, not shortened, and the amendment is what makes that readable in the value: four
        // whole beats, because the ring passes that head by a quarter beat.
        CHECK(shown[0] == Fraction{4});
    }
}

// SCOPE, and it is the whole of what the law still asks besides the landmark. The picking hand and
// a silently-held finger were never MEMBERS of what a grip states — a grip states where the
// FRETTING hand is, so a tap says nothing about whether that hand is still down. Under the
// universal curtain (user ruling 2026-09-07) this is the ONLY population left with an un-rested
// standing tail besides a ring still stating at its end, which is what makes the case sharper than
// it was: with coverage gone, the attack is the last thing that can withhold a verdict.
TEST_CASE("A tap and a silent hold are no members the curtain may take", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // Two rings both ending at beat five, and the second one's ATTACK is the only variable in the
    // pair.
    const auto figure = [](const NoteAttack attack) {
        std::vector<ChartNote> saved{
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 2), 3, Fraction{3}, 9),
        };
        saved[1].attack = attack;
        return saved;
    };

    SECTION("a picked partner ending with it is hidden with it")
    {
        const std::vector<ChartNote> saved = figure(NoteAttack::Pick);
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        CHECK(hidden[1]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — the partner's notated three
        // beats, nothing being struck after it.
        CHECK(shown[1] == Fraction{3});
    }

    SECTION("the tap's own ring is taken by nothing, and the ring under it still hides")
    {
        const std::vector<ChartNote> saved = figure(NoteAttack::Tap);
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        // The tap reaches the very end its picked twin above is hidden for, and NO verdict is
        // passed on it: the attack is the only thing that moved. Since the execution-form
        // amendment both twins present the same three beats, so the pair reads on `hidden` alone
        // — which is the membership fact this section is about.
        CHECK(shown[1] == Fraction{3});
        CHECK_FALSE(hidden[1]);
        // And the ring underneath it is judged on its own account — the tap standing inside it is
        // no part of the verdict now that CROSSING is deleted. Rested, not shortened: rule 1 lets
        // it pass the tap's head and nothing else binds, so its ribbon is four whole beats.
        CHECK(shown[0] == Fraction{4});
        CHECK(hidden[0]);
    }

    SECTION("a silently-held stop has no ring to hide and takes nothing from its neighbour")
    {
        const std::vector<ChartNote> saved{
            note(at(1, 1), 1, Fraction{4}),
            heldStop(at(1, 2), 2, 7),
        };

        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);

        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        // The neighbour states nothing of its own and the board rests it; the hold beside it
        // neither adds to that verdict nor gets one of its own.
        CHECK(hidden[0]);
        CHECK_FALSE(hidden[1]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — four whole beats, because a
        // silent hold draws no head and so binds nothing in front of it.
        CHECK(shown[0] == Fraction{4});
        // The one zero left in this figure is the hold's own: it has no ring to present at all,
        // which is the emptiness the amendment leaves untouched.
        CHECK(shown[1] == Fraction{});
    }
}

// THE ATOM IS THE STROKE, matching rule 3's: every string of a chord rings from one stroke, so one
// stroke gets ONE tail verdict — a CONJUNCTION over the members whose tails are still standing. A
// chord showing a ribbon on the string that stopped and none on the string still sounding is a
// picture no strum makes, and it is exactly what a per-member verdict would draw here.
TEST_CASE("Co-struck plain members each rest", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // A co-struck pair with a head between them. Only the SECOND member's ring length moves — the
    // span the pair used to stand under is irrelevant since the curtain became universal
    // (2026-09-07), so it is gone from the fixture.
    const auto strum = [](const Fraction partner_ring) {
        return std::vector<ChartNote>{
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 1), 2, partner_ring, 7),
            note(at(1, 2), 3, Fraction{1}, 9),
        };
    };

    SECTION("both members ending together are hidden")
    {
        const std::vector<ChartNote> saved = strum(Fraction{4});
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);
        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // Two plain members under one stroke, each resting on its own — since 2026-09-07 the atom
        // is the member, and the stroke's one shared verdict is gone. The law empties nothing, so
        // the presented rings say nothing about the verdict; only the hidden set does.
        CHECK(hidden[0]);
        CHECK(hidden[1]);
        // Rested, not shortened: both rings pass the head a beat in and nothing binds them after,
        // so each presents its notated four beats — the rules-1-to-4 form.
        CHECK(shown[0] == Fraction{4});
        CHECK(shown[1] == Fraction{4});
    }

    SECTION("one member outliving its partner rests like it")
    {
        const std::vector<ChartNote> saved = strum(Fraction{9, 2});
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);
        REQUIRE(shown.size() == 3);
        // The longer member rests like its partner — no inverted chord, and no ribbon popping
        // long out of a resting strum. (The spill amendment bought this while coverage still
        // mattered; the universal curtain makes it structural.)
        CHECK(shown[0] == Fraction{4});
        CHECK(shown[1] == Fraction{9, 2});
        CHECK(hidden[0]);
        CHECK(hidden[1]);
    }
}

// PRESENCE — what the ring is still doing at its end. A ring STILL STATING when it stops (a
// bend held out, a shake that never ends, tremolo, a slide-out) never rests; a statement that
// FINISHES rests from where it finished, its stated portion always visible before the landmark —
// a handover included, whose transfer finishes at the takeover, so its landmark is its ribbon's
// own end and every pixel of it stays.
TEST_CASE("A ring still stating at its end never rests; a finished statement does", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a statement running to the ring's end keeps the whole ribbon standing")
    {
        // The never-rests family, one member per channel: a bend held to the end, a shake that
        // never stops, and tremolo — each still stating at its own end, so no landmark exists
        // for the curtain to own past. The partner is the accounted control.
        const auto verdicts = [&map](ChartNote stating) {
            return restedOffsetsOf({std::move(stating), note(at(1, 2), 2, Fraction{3}, 7)}, map);
        };
        ChartNote bent = note(at(1, 1), 1, Fraction{4});
        bent.bend = 2.0;
        ChartNote shaking = note(at(1, 1), 1, Fraction{4});
        shaking.vibrato = VibratoState::Narrow;
        ChartNote hammering = note(at(1, 1), 1, Fraction{4});
        hammering.tremolo = true;
        ChartNote sliding = note(at(1, 1), 1, Fraction{4});
        sliding.slide_out = 1;

        CHECK_FALSE(verdicts(bent)[0].has_value());
        CHECK_FALSE(verdicts(shaking)[0].has_value());
        CHECK_FALSE(verdicts(hammering)[0].has_value());
        CHECK_FALSE(verdicts(sliding)[0].has_value());
    }

    SECTION("a statement that finishes rests from where it finished")
    {
        // The same bend released mid-ring: the channel goes plain at the release, so the stated
        // portion stays always visible and the remainder rests from that landmark.
        ChartNote released = note(at(1, 1), 1, Fraction{4});
        released.bend = 2.0;
        released.keyframes = {Keyframe{.offset = Fraction{2}, .bend = 0.0}};

        const std::vector<std::optional<Fraction>> rests =
            restedOffsetsOf({std::move(released), note(at(1, 2), 2, Fraction{3}, 7)}, map);
        CHECK(rests[0] == std::optional{Fraction{2}});
    }

    SECTION("a glide on the tail keeps the whole ring, not a length floored on the statement")
    {
        // The same figure with and without one fret statement. Under drop-only the law has no
        // length authority at all, so the marked ring simply draws what the chart says.
        const auto figure = [](const std::vector<std::pair<Fraction, int>>& path) {
            return std::vector<ChartNote>{
                travellingAt(note(at(1, 1), 1, Fraction{4}), path),
                note(at(1, 2), 2, Fraction{3}, 7),
            };
        };

        const std::vector<ChartNote> marked = figure({{Fraction{2}, 12}});
        const std::vector<ChartNote> plain = figure({});
        const std::vector<Fraction> gliding = presentedSustains(marked, map);
        const std::vector<Fraction> planted = presentedSustains(plain, map);
        const std::vector<std::optional<Fraction>> gliding_rests = restedOffsetsOf(marked, map);
        const std::vector<std::optional<Fraction>> planted_rests = restedOffsetsOf(plain, map);

        REQUIRE(gliding.size() == 2);
        REQUIRE(planted.size() == 2);
        REQUIRE(gliding_rests.size() == 2);
        REQUIRE(planted_rests.size() == 2);
        CHECK(gliding[0] == Fraction{4});
        // THE SPLIT (user ruling 2026-09-06): the glide lands at offset two and rings plain to
        // its end, so the statement stays always visible and the plain remainder rests from the
        // landing — the curtain owns everything past the last always-visible landmark.
        CHECK(gliding_rests[0] == std::optional{Fraction{2}});
        // The control differs by that one keyframe and by nothing else: the plain ring states
        // nothing, so it rests from its head. The law empties nothing either way — both present
        // the same rules-1-to-4 four beats.
        CHECK(planted[0] == Fraction{4});
        CHECK(planted_rests[0] == std::optional{Fraction{}});
    }

    SECTION("a handover keeps its whole ribbon where a natural death rests from its head")
    {
        // The discriminating pair the handover exists for: identical rings, differing only in the
        // successor's STORED claim. Duration cannot tell a transfer from a release, which is why
        // the landmark reads the claim rather than the length. Under the universal curtain
        // (2026-09-07) the pair is cleaner still — both halves rest whatever any furniture says,
        // so the claim is the only thing that can move the landmark at all.
        const auto figure = [](const ChartNote& closer) {
            return std::vector<ChartNote>{
                note(at(1, 1), 1, Fraction{2}, 5),
                note(at(1, 2), 2, Fraction{1}, 7),
                closer,
            };
        };
        const std::vector<ChartNote> handed = figure(connected(at(1, 3), 1, Fraction{1}, 3));
        const std::vector<ChartNote> released = figure(note(at(1, 3), 1, Fraction{1}, 3));

        const std::vector<Fraction> junction = presentedSustains(handed, map);
        const std::vector<std::optional<Fraction>> junction_rests = restedOffsetsOf(handed, map);
        const std::vector<Fraction> death = presentedSustains(released, map);
        const std::vector<std::optional<Fraction>> death_rests = restedOffsetsOf(released, map);

        REQUIRE(junction.size() == 3);
        REQUIRE(death.size() == 3);
        // The handover keeps the ring, and rule 1 then binds it at the successor's own head — the
        // ordinary trim, applied to the ring the chart states. Its statement finishes THERE, at
        // the takeover, so it rests from its ribbon's own end: the whole 7/4 is stated portion and
        // the curtain owns none of it.
        CHECK(junction[0] == Fraction{7, 4});
        CHECK(junction_rests[0] == std::optional{Fraction{7, 4}});
        // The natural death states nothing of its own, so the board rests it from the head.
        // THE EXECUTION-FORM AMENDMENT is at its sharpest here: the
        // verdict no longer empties the tail, so the released ring presents the very same
        // rules-1-to-4 trim as the handover — 7/4, rule 1's margin short of the successor's head —
        // and the whole of what the claim buys is the landmark.
        CHECK(death_rests[0] == std::optional{Fraction{}});
        CHECK(death[0] == Fraction{7, 4});
        CHECK(death[0] == junction[0]);
        // The partner rests from its head either way, so neither answer above is the law simply
        // doing nothing.
        CHECK(junction_rests[1] == std::optional{Fraction{}});
        CHECK(death_rests[1] == std::optional{Fraction{}});
    }
}

// VERDICT-ONLY, AND LAST (the execution-form amendment, user ruling 2026-09-03). The law judges the
// tails rules 1 through 4 left standing — it empties none of them any more — and skips every tail
// those rules already emptied, so a zero from rule 3 or rule 4 never enters the hidden set, which
// is what keeps a staccato eighth and a dead chug out of the hold channel's span extension.
TEST_CASE("Emptiness the presentation rules own never enters the hidden set", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a dead member is emptied by rule 4, and its partner is judged alone")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 1), 2, Fraction{4}, 7),
            note(at(1, 2), 3, Fraction{4}, 9),
        };
        saved[1].dead = true;

        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);

        REQUIRE(shown.size() == 3);
        // The two emptinesses used to look alike, and telling them apart is why the verdict is
        // published. Since the execution-form amendment only ONE of them is still a zero: the law
        // empties nothing, so the hidden member presents its rules-1-to-4 form — four whole beats,
        // its ring passing the head a beat in with nothing binding it after.
        CHECK(hidden[0]);
        CHECK(shown[0] == Fraction{4});
        // Rule 4's zero, which the law never sees and never marks.
        CHECK_FALSE(hidden[1]);
        CHECK(shown[1] == Fraction{});
    }

    SECTION("an effect-free member on the bound is dropped by rule 3, never hidden")
    {
        // Plucks ringing exactly the bound: rule 3 drops both, in a figure the law IS live in —
        // the long member above them is hidden, so nothing here passes by the law being inert.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 2, Fraction{4}, 7),
            note(at(1, 3), 1, Fraction{1, 2}, 5),
            note(at(1, 3, Fraction{1, 2}), 1, Fraction{1, 2}, 3),
        };

        const std::vector<Fraction> shown = presentedSustains(saved, map);
        const std::vector<bool> hidden = hiddenOf(saved, map);

        REQUIRE(shown.size() == 3);
        CHECK(hidden[0]);
        CHECK(shown[1] == Fraction{});
        CHECK(shown[2] == Fraction{});
        CHECK_FALSE(hidden[1]);
        CHECK_FALSE(hidden[2]);
    }
}

// THE HOLD CHANNEL reads the verdict, not the tail: a COVERED hidden member holds its OWN STORED
// RING at the least, never the ribbon the presentation rules sized for it.
//
// THE HOLD IS THE TENURE (user sighting 2026-09-03, overruling the brief own-ring reading the
// covered comparison shipped with): while the grip is held the board pins what is held, so a
// hidden member under a span is held to that span's reach — the restrike interior included, whose
// own ring the restrike replaced without the finger ever lifting. In this lone figure the two
// answers coincide (the ring dies where the span closes), and one load-bearing pin is left: the
// rules-1-to-4 margin trim, which the execution-form amendment turned from a discarded
// intermediate into the ribbon the member actually presents — so it is now the number the
// extension is likeliest to pick up. The COVERAGE key is what the universal curtain left standing
// here (user ruling 2026-09-07): resting no longer implies a span, so the stored-ring floor moved
// inside the covered walk and a lone resting note keeps its presented tail instead.
TEST_CASE("A hidden member is held to its span's reach", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // A lone four-beat ring dying at the span's close, with the statement that closes it standing
    // on the next downbeat: rule 1 binds the ribbon a margin short of that head, so the stored
    // ring and the presented ribbon are two different numbers and the hold has to name the first.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{4}),
        note(at(2, 1), 2, Fraction{1}, 7),
    };

    const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
    const std::vector<bool> hidden = hiddenOf(saved, map);
    const std::vector<Fraction> shown = presentedSustains(saved, map);

    REQUIRE(holds.size() == 2);
    REQUIRE(hidden.size() == 2);
    REQUIRE(shown.size() == 2);
    CHECK(hidden[0]);
    // The execution-form amendment: the verdict no longer empties the tail; hidden means the board
    // rests it, and the value is the rules-1-to-4 form — 15/4, a quarter beat short of the closing
    // statement's head.
    CHECK(shown[0] == Fraction{15, 4});
    // The span's reach from its onset: four beats. NOT the 15/4 the margin trim leaves standing on
    // the ribbon. (The presented zero this used to be pinned against is gone with the amendment,
    // so the trim is the whole of what the extension must not pick up.)
    CHECK(holds[0] == Fraction{4});
    CHECK(holds[0] != Fraction{15, 4});
    // The closing statement stands exactly at the seam its span reaches, so it is covered there
    // too; its plain ring rests like everything else, and the reach behind it adds nothing, so its
    // hold is its own stored ring.
    CHECK(hidden[1]);
    CHECK(holds[1] == Fraction{1});
}

// THE HANDOVER UNPINS THE SOURCE (user law 2026-09-06, "pinned heads reflect the current
// SOUNDING state"): a note the next strike on its string takes the sound from — a pull-off or a
// hammer-on source — holds for exactly its stored ring, never the grip's tenure, because the
// destination's own head takes the display over where it lands. The ring IS the takeover by
// construction (\ref sustainBoundOf caps it there, \ref predecessorHoldReaches demands it reach
// there), so no second instant exists to compute. Both legato directions, because the source is
// `hands_over` either way; the base grip beside it keeps the full span pin.
TEST_CASE("A handed-over source's head unpins at the takeover", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    // The base grip is str5 fret6, struck once and held under the span. On str4 the hand reaches
    // to 7 and comes back — the reach is the handover source whose head must not persist. Its
    // stored ring ends exactly on the destination (legato adjacency), so both wrong answers sit
    // strictly apart from the right one: the margin trim leaves a SHORTER presented tail, and
    // the span extension would pin it clear to the reach.
    const auto figure = [&](ChartNote reach, ChartNote destination) {
        return std::vector<ChartNote>{
            note(at(1, 1), 5, Fraction{4}, 6),
            std::move(reach),
            std::move(destination),
        };
    };

    SECTION("a pull-off source holds only to the pull, the grip holds the span")
    {
        const std::vector<Fraction> holds = holdsUnderSpans(
            figure(note(at(1, 1), 4, Fraction{1}, 7), connected(at(1, 2), 4, Fraction{1}, 5)),
            shapes,
            map);
        REQUIRE(holds.size() == 3);
        CHECK(holds[0] == Fraction{4}); // base grip: full span tenure
        CHECK(holds[1] == Fraction{1}); // the 7: its ring, ending on the pull — not the span's 4
    }

    SECTION("a hammer-on source unpins at the hammer just the same")
    {
        const std::vector<Fraction> holds = holdsUnderSpans(
            figure(note(at(1, 1), 4, Fraction{1}, 5), connected(at(1, 2), 4, Fraction{1}, 7)),
            shapes,
            map);
        REQUIRE(holds.size() == 3);
        CHECK(holds[0] == Fraction{4});
        CHECK(holds[1] == Fraction{1}); // the 5: its ring, ending on the hammer
    }
}

// THE CO-STRUCK HANDOVER (user sighting 2026-09-06, the open-chord intro's first stroke): a
// pull-off's source struck TOGETHER with a member that rings on past it. Read as a
// statement still in progress the handover refused the verdict, and the stroke conjunction of the
// day then made the partner's whole ring draw in front of the curtain that owned it. A handover is
// a statement that FINISHES — at the takeover, where the successor picks the sound up — so it is
// the finished-statement split with an empty remainder: it rests from its ribbon's own end, keeps
// every pixel of its ribbon, and the partner rests. The conjunction itself went on 2026-09-07 (the
// atom is the member), which the veto section below now pins from the other side.
TEST_CASE("A co-struck handover rests from its own end, and its partner rests", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{5, 2}},
    };
    // String 2 rings two beats from the stroke; string 5's source rings half a beat and hands
    // over to the open string, which rings on beneath the opens that follow.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 2, Fraction{2}, 3),
        note(at(1, 1), 5, Fraction{1, 2}, 3),
        connected(at(1, 1, Fraction{1, 2}), 5, Fraction{2}, 0),
        note(at(1, 2), 4, Fraction{3, 2}, 0),
        note(at(1, 2, Fraction{1, 2}), 3, Fraction{1}, 0),
    };

    SECTION("the partner rests from its head, the handover from its end, the release rests too")
    {
        const std::vector<std::optional<Fraction>> rests = restedOffsetsOf(saved, map);
        const std::vector<Fraction> shown = presentedSustains(saved, map);
        REQUIRE(rests.size() == 5);
        REQUIRE(shown.size() == 5);
        // Both members rest. The partner's whole ribbon is plain, so its landmark is the head;
        // the handover's whole ribbon is the transfer, so its landmark is the ribbon's own end —
        // nothing of it is ever curtained.
        //
        // NO SPAN IS STATED, and that is the 2026-09-07 re-pin: an "uncovered stroke" section
        // stood beside this one, running the identical figure with the only span standing LATER,
        // and pinned that nothing rested until a ribbon ran under it — the partner and the
        // handover drew whole, and only the release reached the bracket. The universal curtain
        // makes the two fixtures the same fixture, so the second is deleted and these three
        // verdicts are what it now asserts.
        CHECK(rests[0] == std::optional{Fraction{}});
        CHECK(rests[1] == std::optional{Fraction{1, 4}});
        CHECK(rests[2] == std::optional{Fraction{}});
        // Rested, never shortened: the partner presents its notated two beats, and the handover's
        // ribbon is bound at the takeover by rule 1 — the margin short of the release's head —
        // which is exactly where its landmark sits.
        CHECK(shown[0] == Fraction{2});
        CHECK(shown[1] == Fraction{1, 4});
    }

    SECTION("the partner's hold follows its verdict: pinned to the span's reach")
    {
        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == 5);
        CHECK(holds[0] == Fraction{5, 2});
        CHECK(holds[1] == Fraction{1, 2}); // the source: its ring, ending on the pull
    }

    SECTION("a partner still STATING at its end draws alone — its plain stackmate rests")
    {
        // THE ATOM IS THE MEMBER (user ruling 2026-09-07: "the curtain should apply to everything
        // in the span that doesn't carry technique info"). A co-struck member holding a bend to
        // the ring's end never rests — a statement in progress — and until this ruling its veto
        // drew its plain partner whole beside it. Now the bend draws and the plain partner rests:
        // the ribbon carrying information is the one that stays visible.
        ChartNote held_bend = note(at(1, 1), 2, Fraction{2}, 3);
        held_bend.bend = 1.0;
        const std::vector<ChartNote> stating = {held_bend, note(at(1, 1), 5, Fraction{2}, 3)};
        const std::vector<bool> hidden = hiddenOf(stating, map);
        REQUIRE(hidden.size() == 2);
        CHECK_FALSE(hidden[0]);
        CHECK(hidden[1]);
    }

    SECTION("a handover still shaking at its end finishes all the same — the takeover ends it")
    {
        // THE PRECEDENCE, pinned: the source shakes right up to the pull-off. Read as a statement
        // in progress it would veto the stroke exactly as the held bend above does; but the
        // takeover terminates the shake — the successor has the string — so the handover's
        // landmark is its ribbon's end either way, its ink is identical, and the partner rests.
        std::vector<ChartNote> shaking = saved;
        shaking[1].vibrato = VibratoState::Narrow;
        const std::vector<std::optional<Fraction>> rests = restedOffsetsOf(shaking, map);
        REQUIRE(rests.size() == 5);
        CHECK(rests[0] == std::optional{Fraction{}});
        CHECK(rests[1] == std::optional{Fraction{1, 4}});
    }
}

// THE DERIVED FIGURES. The cases above state their notes flat; these let the DERIVATION answer,
// which is the only way to pin what the law does to the shapes real material implies — and, since
// the grip-tenure rebuild, the only way to see that the spans themselves are no longer the ones
// the old machine built.
TEST_CASE("A derived box span takes exactly the rings it covers", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a box span's uneven rings keep the tails presentation gave them")
    {
        // One strum, two unequal rings, nothing carried or claimed and no tap: the shape sounds
        // whole, so the span arrives as a BOX and its reach is the SHORTER member's ring. Both
        // members are plain to their ends, so both rest — the longer one included, which the spill
        // amendment first bought and the universal curtain now makes structural.
        const SpanFigure figure = spanFigure(
            {
                note(at(1, 1), 1, Fraction{2}),
                note(at(1, 1), 2, Fraction{4}, 7),
            },
            map);

        REQUIRE(figure.shapes.size() == 1);
        CHECK(figure.shapes[0].sustain == Fraction{2});
        REQUIRE(figure.arrivals.size() == 1);
        CHECK_FALSE(figure.arrivals[0]);
        REQUIRE(figure.presented.size() == 2);
        CHECK(figure.presented[0].sustain == Fraction{2});
        CHECK(figure.presented[1].sustain == Fraction{4});
        CHECK(figure.hidden[0]);
        CHECK(figure.hidden[1]);
    }

    SECTION("a quarter-note chug chain goes ribbonless end to end")
    {
        // THE RESTRIKE CHAIN, derived: the same grip struck on three consecutive beats is ONE span
        // (rule 8 — a restatement of the same grip continues it), reaching the LAST strike's rings
        // at beat four. The earlier strikes' rings die at their own restrikes, well inside that
        // span — the population that falsified rule 11's "close is the minimum" proof — and the
        // law rests every one of them, which is exactly the consequence
        // `span-derivation-ground-up.md` prices beside the plain sustained chord: quarter-note
        // chug chains rest ribbonless, the repeat heads and the rails stating the rhythm.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1}),
            note(at(1, 1), 2, Fraction{1}, 7),
            note(at(1, 2), 1, Fraction{1}),
            note(at(1, 2), 2, Fraction{1}, 7),
            note(at(1, 3), 1, Fraction{1}),
            note(at(1, 3), 2, Fraction{1}, 7),
        };
        const SpanFigure figure = spanFigure(saved, map);
        const std::vector<Fraction> bare = presentedSustains(saved, map);

        REQUIRE(figure.shapes.size() == 1);
        CHECK(figure.shapes[0].position == at(1, 1));
        CHECK(figure.shapes[0].sustain == Fraction{3});
        REQUIRE(figure.presented.size() == 6);
        REQUIRE(bare.size() == 6);
        for (std::size_t index = 0; index < 6; ++index)
        {
            CHECK(figure.hidden[index]);
            // The execution-form amendment: the verdict no longer empties the tail; hidden means
            // the board rests it, and the value is the rules-1-to-4 form — the picture with no
            // furniture at all, member for member.
            CHECK(figure.presented[index].sustain == bare[index]);
        }
        // That form concretely: each strike but the last binds on the next one a beat away and
        // trims to the quarter-beat margin, while the last has nothing in front of it and rings
        // its whole beat. This is the rhythm the ribbons no longer duplicate at distance.
        CHECK(bare[0] == Fraction{3, 4});
        CHECK(bare[4] == Fraction{1});
    }

    SECTION("a chug under a box still shows nothing, because presentation emptied it")
    {
        // The discrimination: what keeps a chugged riff clean under a box is rule 3's earning, not
        // anything span-scoped, and the law never sees an empty tail.
        const SpanFigure figure = spanFigure(
            {
                note(at(1, 1), 1, Fraction{1, 4}),
                note(at(1, 1), 2, Fraction{1, 4}, 7),
            },
            map);

        REQUIRE(figure.presented.size() == 2);
        CHECK(figure.presented[0].sustain == Fraction{});
        CHECK(figure.presented[1].sustain == Fraction{});
        CHECK_FALSE(figure.hidden[0]);
        CHECK_FALSE(figure.hidden[1]);
    }

    SECTION("a carried ring and the strum inside it are hidden together")
    {
        // A ring carrying into the strum's onset joins the posture; the DATING RULE puts the
        // span's front at that member's own onset, so one span runs from the carry's onset to the
        // close all three rings share. Under the deleted CROSSING conjunct the strum kept its
        // tails — nothing sounded inside them — and under the own-span law the whole figure rests
        // ribbonless. This is the sighting headline of the rebuild in three notes.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 2, Fraction{4}, 7),
            note(at(1, 3), 1, Fraction{2}),
            note(at(1, 3), 3, Fraction{2}, 9),
        };
        const SpanFigure figure = spanFigure(saved, map);
        const std::vector<Fraction> bare = presentedSustains(saved, map);

        REQUIRE(figure.shapes.size() == 1);
        CHECK(figure.shapes[0].position == at(1, 1));
        CHECK(figure.shapes[0].sustain == Fraction{4});
        REQUIRE(figure.presented.size() == 3);
        CHECK(figure.hidden[0]);
        CHECK(figure.hidden[1]);
        CHECK(figure.hidden[2]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form. Nothing binds any of these three
        // — the carry passes the strum's heads, and the strum has nothing after it — so all three
        // present their notated rings.
        CHECK(figure.presented[0].sustain == bare[0]);
        CHECK(figure.presented[1].sustain == bare[1]);
        CHECK(figure.presented[2].sustain == bare[2]);
        CHECK(bare == std::vector<Fraction>{Fraction{4}, Fraction{2}, Fraction{2}});
    }
}

// THE SIGHTED FIGURE (user, 2026-09-01), re-derived under grip tenure. A real let-ring texture
// opens with a STRUMMED PAIR whose rings the later plucks accumulate over, all ending together at
// the statement's boundary. The old machine SPLIT the span at every growth, so the founding rings
// lived in a DIFFERENT span from the heads they crossed and only a figure walk could carry them.
// Rule 8 makes growth ACCUMULATION IN PLACE — a stop the grip lacks joins it and breaks nothing —
// so there is ONE span and the law needs no cross-span vocabulary at all. That inversion is
// deliberate: 2026-08-25's "growth keeps splitting" ruling was narrowed to the authored-marker
// context on 2026-09-04, and the strummed-pair split is the census delta the narrowing buys.
TEST_CASE("A founding strum grows in place and the whole figure goes ribbonless", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // Two strings strummed at beat one; single plucks on new strings at beats two and three; every
    // ring runs to beat five.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{4}, 0),
        note(at(1, 1), 2, Fraction{4}, 7),
        note(at(1, 2), 3, Fraction{3}, 9),
        note(at(1, 3), 4, Fraction{2}, 5),
    };
    const SpanFigure figure = spanFigure(saved, map);
    const std::vector<Fraction> bare = presentedSustains(saved, map);

    // ONE span, fronted on the strum and reaching the close every ring shares.
    REQUIRE(figure.shapes.size() == 1);
    CHECK(figure.shapes[0].position == at(1, 1));
    CHECK(figure.shapes[0].sustain == Fraction{4});
    // It still arrives ARPEGGIO, and now on its own account: it SOUNDS IN PARTS, the plucks stating
    // it after its front. That class used to be carried by the split's successor.
    REQUIRE(figure.arrivals.size() == 1);
    CHECK(figure.arrivals[0]);
    REQUIRE(figure.presented.size() == 4);
    REQUIRE(bare.size() == 4);
    CHECK(figure.hidden[0]);
    CHECK(figure.hidden[1]);
    CHECK(figure.hidden[2]);
    // THE CLOSER GOES WITH THEM (2026-09-04 reversal): every ring dies at the one close.
    CHECK(figure.hidden[3]);
    // The execution-form amendment: the verdict no longer empties the tail; hidden means the board
    // rests it, and the value is the rules-1-to-4 form. Every ring here passes the heads after it,
    // so the whole figure presents its notated lengths.
    for (std::size_t index = 0; index < 4; ++index)
    {
        CHECK(figure.presented[index].sustain == bare[index]);
    }
    CHECK(bare == std::vector<Fraction>{Fraction{4}, Fraction{4}, Fraction{3}, Fraction{2}});
}

// A DRY ARPEGGIO — the stepped look the retired staircase used to invent — rests whole: every
// step's ring ends at its own next head and states nothing of its own (the signed ruling: "hide
// ALL tails except the explicit exceptions"). At distance the rhythm those stubs duplicate is the
// heads' own, so the bracket, the rails, and the hold-pinned heads state the tenure; the
// execution-form amendment keeps each stub in the stream, for the lane always and for the board as
// the head approaches.
//
// THE HOLD DISCRIMINATION rides here because this is the figure where it is visible: a hidden
// step is held to the SPAN'S reach from its own onset, never to its one-beat ring (user sighting
// 2026-09-03: the grip is held, so the board pins what is held through the whole tenure — the
// pluck replaced the sound, not the finger).
TEST_CASE("A dry arpeggio goes ribbonless under its span", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // One pluck per beat, each ring ending exactly where the next begins.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{1}),
        note(at(1, 2), 2, Fraction{1}, 7),
        note(at(1, 3), 3, Fraction{1}, 9),
        note(at(1, 4), 4, Fraction{1}, 5),
    };
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    const std::vector<Fraction> shown = presentedSustains(saved, map);
    const std::vector<bool> hidden = hiddenOf(saved, map);
    const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);

    REQUIRE(shown.size() == 4);
    REQUIRE(hidden.size() == 4);
    REQUIRE(holds.size() == 4);
    for (std::size_t index = 0; index < 4; ++index)
    {
        CHECK(hidden[index]);
        // The span's reach from each step's own onset — every finger stays down to the one close,
        // which is the discrimination against the one-beat stored rings and, on the three steps
        // rule 1 trims, against the presented ribbons the amendment restored.
        CHECK(holds[index] == Fraction{static_cast<int>(4 - index)});
    }
    // The execution-form amendment concretely: the verdict empties no tail, so each step but the
    // last binds on the next head a beat away and trims to the margin, and the last rings its
    // whole beat.
    CHECK(shown[0] == Fraction{3, 4});
    CHECK(shown[3] == Fraction{1});
}

} // namespace rock_hero::common::core
