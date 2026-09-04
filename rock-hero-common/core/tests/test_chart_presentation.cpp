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
// quarter beat (1/16 of a whole note) and the kept-sustain bound is one beat (a quarter note).
[[nodiscard]] TempoMap fourFourMap()
{
    return TempoMap::defaultMap(TimeDuration{60.0});
}

// Measures 1-2 are 4/4, measure 3 onward is 6/8. Both bounds are whole-note-referenced, so the
// meter change moves them together: the margin goes from 1/4 beat to 1/2 beat and the kept-sustain
// bound from 1 beat to 2 beats. That is what makes a 6/8 trim a different number, not a different
// rule.
[[nodiscard]] TempoMap meterChangeMap()
{
    return TempoMap{
        {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
         TimeSignatureChange{.measure = 3, .numerator = 6, .denominator = 8}},
        {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
         BeatAnchor{.measure = 8, .beat = 1, .seconds = 24.0}},
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
    const ChartShapes derived = deriveChartShapes(saved, chartClaimedStops(connections), tempo_map);
    SpanFigure figure;
    figure.arrivals = chartShapeArrivals(saved, derived.shapes, tempo_map);
    ChartPresentation presentation = presentedChartNotes(connections, derived, tempo_map);
    figure.presented = std::move(presentation.notes);
    figure.hidden = std::move(presentation.hidden);
    figure.shapes = derived.shapes;
    return figure;
}

// A posture naming exactly the listed strings, at the open fret. Conjunct 2 asks whether the
// furniture names the ring's string, and a hand-written span with no posture behind it names
// nothing at all — so a case about any OTHER conjunct has to supply one, or it would be answering
// conjunct 2 by accident. The cases that are about conjunct 2 use the real derivation instead.
[[nodiscard]] ChartPosture postureHolding(const std::vector<int>& strings)
{
    ChartPosture posture;
    posture.frets.assign(static_cast<std::size_t>(g_max_chart_strings), std::nullopt);
    for (const int string : strings)
    {
        posture.frets[static_cast<std::size_t>(string - 1)] = 0;
    }
    return posture;
}

// Spans that all hold ONE posture, for the cases that state their SPANS themselves.
[[nodiscard]] ChartShapes spansHolding(
    const std::vector<ChartShape>& shapes, const std::vector<int>& strings)
{
    return ChartShapes{
        .shapes = shapes,
        .postures = {postureHolding(strings)},
        .claim_shapes = {},
    };
}

// Spans holding a posture EACH — the seam cases' own fixture. Which span the walk asked is only
// observable where the spans give different answers, so a case about the seam has to name
// different strings on either side of it; under one shared posture both sides answer conjunct 2
// the same and the case would pass whichever span it reached.
[[nodiscard]] ChartShapes spansEachHolding(
    const std::vector<std::pair<ChartShape, std::vector<int>>>& spans)
{
    ChartShapes furniture;
    for (const auto& [shape, strings] : spans)
    {
        furniture.shapes.push_back(shape);
        furniture.shapes.back().posture = furniture.postures.size();
        furniture.postures.push_back(postureHolding(strings));
    }
    return furniture;
}

[[nodiscard]] ChartShapes statedSpans(const std::vector<ChartShape>& shapes)
{
    std::vector<int> every;
    for (int string = 1; string <= g_max_chart_strings; ++string)
    {
        every.push_back(string);
    }
    return spansHolding(shapes, every);
}

// The presented sustains under furniture the case states itself, postures included.
[[nodiscard]] std::vector<Fraction> shownUnder(
    const std::vector<ChartNote>& saved, const ChartShapes& furniture, const TempoMap& tempo_map)
{
    std::vector<Fraction> sustains;
    for (const ChartNote& note :
         presentedChartNotes(chartConnections(saved, tempo_map), furniture, tempo_map).notes)
    {
        sustains.push_back(note.sustain);
    }
    return sustains;
}

// Which tails the law HID under that same furniture, beside \ref shownUnder: a zero sustain is not
// one fact, and every case below that reads a zero has to say which one it means.
[[nodiscard]] std::vector<bool> hiddenUnder(
    const std::vector<ChartNote>& saved, const ChartShapes& furniture, const TempoMap& tempo_map)
{
    return presentedChartNotes(chartConnections(saved, tempo_map), furniture, tempo_map).hidden;
}

// The presented sustains under STATED spans, for the cases that state the span themselves.
[[nodiscard]] std::vector<Fraction> underSpans(
    const std::vector<ChartNote>& saved, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    return shownUnder(saved, statedSpans(shapes), tempo_map);
}

// The same, under spans whose posture names only the listed strings — conjunct 2's own fixture.
[[nodiscard]] std::vector<Fraction> underSpansHolding(
    const std::vector<ChartNote>& saved, const std::vector<ChartShape>& shapes,
    const std::vector<int>& strings, const TempoMap& tempo_map)
{
    return shownUnder(saved, spansHolding(shapes, strings), tempo_map);
}

// Which tails the law HID under stated spans.
[[nodiscard]] std::vector<bool> hiddenUnderSpans(
    const std::vector<ChartNote>& saved, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    return hiddenUnder(saved, statedSpans(shapes), tempo_map);
}

// The presented notes with NO furniture at all: rules 1 through 4 and nothing else, which is what
// every rule-scoped case below is about — and, for the span cases, the picture the law promises
// every un-hidden tail keeps.
[[nodiscard]] std::vector<ChartNote> presentedNotesOf(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    return presentedChartNotes(chartConnections(saved, tempo_map), ChartShapes{}, tempo_map).notes;
}

// The presented sustains with NO furniture at all — and, for the span cases, the picture the law
// promises every un-hidden tail keeps.
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

// The holds a stated span implies, asked the way \ref chartResolutions asks them: the presented
// stream and the law's verdict together, against the stream they came from.
[[nodiscard]] std::vector<Fraction> holdsUnderSpans(
    const std::vector<ChartNote>& saved, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    const ChartConnections connections = chartConnections(saved, tempo_map);
    const ChartPresentation presentation =
        presentedChartNotes(connections, statedSpans(shapes), tempo_map);
    return chartHolds(presentation, connections.saved_notes, shapes, tempo_map);
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
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{7, 8}),
        note(at(1, 1), 2, Fraction{1, 4}, 7),
        note(at(1, 1, Fraction{1, 2}), 3, Fraction{1}, 2),
        note(at(1, 2), 4, Fraction{1}, 3),
    };

    const std::vector<Fraction> presented = presentedSustains(saved, map);
    REQUIRE(presented.size() == saved.size());
    // Seven eighths of a beat passes the onset half a beat in and then binds on the onset a beat
    // in, trimming to that onset's margin — the exemption would have presented all 7/8.
    CHECK(presented[0] == Fraction{3, 4});
    // Its partner is effect-free and a quarter beat long, and the ring-through is UNDER the
    // kept-sustain bound of one beat, so the passed onset is the only thing that can earn this
    // strum a tail. Without that earning input both members would present nothing.
    CHECK(presented[1] == Fraction{1, 4});
    // The onset the ring passed is its own group, earns its own tail at the kept-sustain bound,
    // and passes the onset after it with nothing beyond to bind it.
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
// A tail is earned by a sustain technique, by a deliberate hold, or by an ACTUAL ring reaching the
// kept-sustain bound — the actual ring, never the trimmed one, because the trim is presentation and
// the question is what the chart states.
TEST_CASE("Rule 3 decides one tail verdict for a whole onset group", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{1, 2}),
        note(at(1, 1), 2, Fraction{1, 2}),
        note(at(1, 2), 1, Fraction{1, 2}),
        note(at(1, 2), 2, Fraction{1, 2}),
        note(at(1, 3), 1, Fraction{1}),
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
    // The identical group with no technique anywhere and no member reaching a quarter note is a
    // chug, not a sustain, and presents no tail at all.
    CHECK(presented[2] == Fraction{});
    CHECK(presented[3] == Fraction{});
    // One member notated at the kept-sustain bound earns the group's tails.
    CHECK(presented[4] == Fraction{1});
    CHECK(presented[5] == Fraction{1, 2});
    // A deliberate hold earns them as well, and keeps its own ring whole.
    CHECK(presented[6] == Fraction{3});
    CHECK(presented[7] == Fraction{1, 2});
    CHECK(presented[8] == Fraction{1});
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
    // Half a beat is under the kept-sustain bound, so only the bend earns this strum its tails —
    // and it earns them for the plain partner too.
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

    SECTION("a member that draws a tail keeps its own hold")
    {
        // The first member LEAVES the span, so the stroke draws (the tail law takes no leaving
        // stroke) and every member states its own hold — the drawn-tail case, not the covered one.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{9, 2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
            note(at(2, 1), 3, Fraction{1}, 3),
        };

        const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);
        REQUIRE(holds.size() == saved.size());
        CHECK(holds[0] == Fraction{9, 2});
        CHECK(holds[1] == Fraction{1, 2});
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
        // The seam note leaves the span and draws, so it still states its own hold — the in-case
        // control that the extension takes only what draws nothing.
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
// under the kept-sustain bound, so rule 3 presents no tail and the span rule is what answers.
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

// THE TAIL LAW (grip-tenure law, user-signed 2026-09-04): span furniture may HIDE a tail, never
// shorten one, and it is now ONE COMPARISON — a tail hides exactly where its ring dies AT ITS OWN
// SPAN'S CLOSE and states nothing of its own.
//
// THE EXECUTION-FORM AMENDMENT (user ruling 2026-09-03) is what "hide" means here, and every pin
// below is read through it: the law PUBLISHES the verdict and EMPTIES NOTHING. Hiding is the
// highway's RESTING form — the board suppresses a hidden ribbon at distance and reveals it as the
// head approaches the hit line — while the presented stream carries every member's rules-1-to-4
// tail, which the 2D lane draws always. So the whole presented picture is now what it would be with
// no furniture in the chart, hidden members included, and `hidden` is the only channel the law
// writes. Each case below therefore pins its rested tails at that bare form, and the fixtures that
// used to read a verdict off an emptied tail read it off `hidden` instead.
//
// FOUR CONJUNCTS BECAME ONE, and the cases below carry the history of each. TIME narrowed from a
// cross-span FIGURE walk to the span standing at the note's own onset; STRING and END died as
// PROOFS of the grip-tenure derivation (growth in place, all-members-bound); CROSSING was DELETED
// BY RULING on 2026-09-04, which is what makes a figure's closer ordinary.
//
// The span is STATED here rather than derived, because these cases are about the law's own
// arithmetic and a derived figure would be stating the class and the posture as well. The cases
// that turn on what the derivation produces use `spanFigure` instead (below).
TEST_CASE("A ring dying at its own span's close hides its ribbon", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // One span across the whole measure: its musical close is the next downbeat.
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

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

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // All three rings die at the span's own close, so the span accounts for the whole of each
        // and the rails are the whole statement.
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
        // approaches the hit line — while the presented value keeps its rules-1-to-4 form, which
        // is literally the picture with no furniture at all. So all three hidden members present
        // exactly what the bare stream gives them, and that identity IS the law in one line.
        CHECK(shown == presentedSustains(saved, map));
        // The bare picture concretely, which is what the amendment restored to the ribbon: rule 1
        // leaves all three whole, because each passes the onsets after it.
        CHECK(
            presentedSustains(saved, map) ==
            std::vector<Fraction>{Fraction{4}, Fraction{3}, Fraction{2}});
    }

    SECTION("a mid-span long hold and a span-final one are hidden alike")
    {
        // A four-beat ring covering the span exactly, with one strum inside it, and a second ring
        // struck at beat three that reaches the same close with nothing after it. Under the
        // deleted crossing conjunct the second one drew; under the own-span law the two are the
        // same sentence.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 3), 2, Fraction{2}, 7),
        };

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        CHECK(hidden[1]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value here is the rules-1-to-4 form — the bare picture, which
        // rule 1 leaves whole on both members (each passes every onset after it).
        CHECK(shown == presentedSustains(saved, map));
        CHECK(presentedSustains(saved, map) == std::vector<Fraction>{Fraction{4}, Fraction{2}});
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
// rings and only `hidden` says which one the board rests.
TEST_CASE("A span-final long hold hides its tail like every other member", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // The second member is struck on beat three and rings two beats — to the span's own close, to
    // the tick. The first outlives the span by half a beat and is the control: a leaving ring is
    // the one exception left standing, so it keeps its ribbon and nothing here passes by the law
    // being inert.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{9, 2}),
        note(at(1, 3), 2, Fraction{2}, 7),
    };

    const std::vector<Fraction> shown = underSpans(saved, shapes, map);
    const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

    REQUIRE(shown.size() == 2);
    REQUIRE(hidden.size() == 2);
    CHECK_FALSE(hidden[0]);
    CHECK(hidden[1]);
    // Both present their rules-1-to-4 rings: the leaving one because the law never touched it, the
    // closer because the amendment stopped the verdict emptying anything. Rule 1 binds neither —
    // each passes every onset after it — so the pair is the bare picture exactly.
    CHECK(shown == presentedSustains(saved, map));
    CHECK(shown[0] == Fraction{9, 2});
    CHECK(shown[1] == Fraction{2});
}

// OWN-SPAN, and it is the whole of the law's time question now (user ruling 2026-09-04). The
// FIGURE — the maximal run of spans abutting at their musical closes — is DELETED, along with the
// cross-span stretch walk and the figure id that keyed it: a ring is judged against the ONE span
// standing at its ONSET and against nothing else, so a successor abutting exactly where the ring
// dies carries nothing across the seam. The pair below differs by the span PARTITION alone, which
// is exactly the datum the figure walk used to erase.
TEST_CASE("A ring is judged against its own span and no other", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // One long ring at beat one running to beat five, and two later members.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{4}, 5),
        note(at(1, 3), 2, Fraction{1}, 7),
        note(at(1, 4), 3, Fraction{1}, 8),
    };
    // Two spans tiling exactly at beat three, against the one span covering the same ground.
    const std::vector<ChartShape> tiled = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{2}},
        ChartShape{.position = at(1, 3), .sustain = Fraction{2}},
    };
    const std::vector<ChartShape> whole = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a ring outliving its own span draws, however exactly the successor abuts")
    {
        const std::vector<Fraction> shown = underSpans(saved, tiled, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, tiled, map);

        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // The ring is struck under the first span and dies at the SECOND one's close. The figure
        // walk called that run one figure and hid the ribbon; the own-span law reads a ring that
        // LEFT the grip it was struck in, which is exactly the news the ruling wants inked.
        CHECK(shown[0] == Fraction{4});
        CHECK_FALSE(hidden[0]);
    }

    SECTION("the same ring under ONE span reaching its end is hidden")
    {
        const std::vector<Fraction> shown = underSpans(saved, whole, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, whole, map);

        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // Identical notes; only the partition moved — and since the execution-form amendment the
        // move shows in the VERDICT alone. The law empties no tail, so the ring presents the same
        // rules-1-to-4 four beats as the section above (it passes both later heads); what the one
        // span buys is that the board rests the ribbon.
        CHECK(hidden[0]);
        CHECK(shown[0] == Fraction{4});
    }

    SECTION("a ring whose ONSET stands on open ground is covered by nothing")
    {
        const std::vector<ChartShape> late = {
            ChartShape{.position = at(1, 3), .sustain = Fraction{2}},
        };
        const std::vector<Fraction> shown = underSpans(saved, late, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, late, map);

        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // No span stands at this onset, so the law never reaches a verdict on it — the fact the
        // section is about, and since the amendment the only one the presented value cannot show.
        CHECK_FALSE(hidden[0]);
        CHECK(shown[0] == Fraction{4});
    }
}

// THE SEAM, and only the half of it that survives (user ruling 2026-09-04). Where one span closes
// and the next opens at the same instant, an ONSET stands in the grip that ARRIVED: a note struck
// there is a member of the new shape and not of the one it replaced. The RING-END half of the
// 2026-09-04 ruling has become unaskable — the own-span law looks up exactly one instant, the
// note's onset, so there is no second query left for a seam to disagree with, and
// `SpanCover::stillReaching` was deleted with the question. What used to be pinned there is now
// pinned as the own-span partition pair above.
TEST_CASE("An onset at a seam stands in the span that opens", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // An arpeggio pair whose rings die exactly at the seam, a two-string stab struck AT the seam
    // that rings to its own span's close, and one late pluck that outlives it. The stab's grip
    // names NONE of the arpeggio's strings, which is what makes the two spans distinguishable at
    // all — even though the law no longer reads a posture, the fixture keeps them apart so the
    // spans stay the shape the derivation would build.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{2}),
        note(at(1, 2), 2, Fraction{1}, 7),
        note(at(1, 3), 4, Fraction{2}, 3),
        note(at(1, 3), 5, Fraction{2}, 5),
        note(at(1, 4), 3, Fraction{5, 4}, 9),
    };
    const ChartShapes spans = spansEachHolding({
        {ChartShape{.position = at(1, 1), .sustain = Fraction{2}}, {1, 2, 3}},
        {ChartShape{.position = at(1, 3), .sustain = Fraction{2}}, {4, 5}},
    });

    const std::vector<Fraction> shown = shownUnder(saved, spans, map);
    const std::vector<bool> hidden = hiddenUnder(saved, spans, map);

    REQUIRE(shown.size() == 5);
    REQUIRE(hidden.size() == 5);
    // THE RULING ITSELF: the stab is struck at the seam and rings to the SECOND span's close, so
    // its tails can only be hidden if its onset resolved FORWARD to the grip it is played in.
    // Resolved backward it would be measured against the arpeggio span's close two beats earlier
    // and both members would keep their ribbons.
    CHECK(hidden[2]);
    CHECK(hidden[3]);
    // The arpeggio's own members die at their span's close and go with it — the closer at beat two
    // included, which is the 2026-09-04 reversal.
    CHECK(hidden[0]);
    CHECK(hidden[1]);
    // The execution-form amendment (user ruling 2026-09-03): the verdict no longer empties the
    // tail; hidden means the board rests it, and the value here is the rules-1-to-4 form. The
    // stab's rings pass the late pluck's head by a whole beat and nothing stands after it, so rule
    // 1 binds neither and both present their notated two beats.
    CHECK(shown[2] == Fraction{2});
    CHECK(shown[3] == Fraction{2});
    // Rested, not shortened, and the identity holds on the arpeggio pair too: what they present
    // under the span is exactly rule 1's margin-trimmed stubs with no furniture at all.
    CHECK(shown[0] == presentedSustains(saved, map)[0]);
    CHECK(shown[1] == presentedSustains(saved, map)[1]);
    CHECK(presentedSustains(saved, map)[0] == Fraction{7, 4});
    CHECK(presentedSustains(saved, map)[1] == Fraction{3, 4});
    // The late pluck's ring runs a quarter beat past the second span's close, so it is leaving
    // rather than dying there and keeps its ribbon — the control that keeps this from passing on
    // the law hiding everything.
    CHECK_FALSE(hidden[4]);
    CHECK(shown[4] == Fraction{5, 4});
}

// CONJUNCT 2 — STRING — DIED AS A PROOF, not as a ruling, and this fixture is what keeps the proof
// honest. The conjunct refused to account for a ring on a string the span's posture never named.
// Under grip-tenure derivation growth happens IN PLACE — a stop the grip lacks joins it and breaks
// nothing (rule 8) — so every string a span's members sound IS a stop its posture states, and the
// conjunct had no population left to refuse. `spanNamesString` was deleted with it, and the law
// reads no posture at all: the pair below differs by the posture alone and answers the same way
// twice. The proof is conditional on no non-bounding member class ever returning; if one does,
// this case is where the vacuity stops being true.
TEST_CASE("The tail law never reads a span's posture", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{4}),
        note(at(1, 2), 2, Fraction{4}, 7),
    };

    SECTION("a posture naming the ring's string hides it")
    {
        const std::vector<Fraction> shown = underSpansHolding(saved, shapes, {1, 2}, map);
        const std::vector<bool> hidden = hiddenUnder(saved, spansHolding(shapes, {1, 2}), map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — four whole beats, because the
        // ring passes the one head after it and nothing else binds. What the posture pair reads
        // is therefore the VERDICT, which is the fact this case was always about.
        CHECK(shown[0] == Fraction{4});
        // The partner's ring outlives the span by a beat, so no posture can take it — the control
        // that keeps the sections below from agreeing vacuously.
        CHECK(shown[1] == Fraction{4});
        CHECK_FALSE(hidden[1]);
    }

    SECTION("a posture silent about it hides it just the same")
    {
        const std::vector<Fraction> shown = underSpansHolding(saved, shapes, {2}, map);
        const std::vector<bool> hidden = hiddenUnder(saved, spansHolding(shapes, {2}), map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        // The same verdict and the same rules-1-to-4 form as the section above, from a posture
        // that names neither ring's string: the pair differs by the posture alone.
        CHECK(shown[0] == Fraction{4});
        CHECK(shown[1] == Fraction{4});
        CHECK_FALSE(hidden[1]);
    }
}

// THE ONE COMPARISON, both ways round: a ring its own span COVERS — dying at or inside the close —
// is hidden, and only a ring OUTLIVING the span draws.
//
// CONJUNCT 3 — END — DIED AS A PROOF too. It admitted a second provenance beside the span's own
// close: a ring ending at its own string's next sounding onset. The law's proof for dropping it is
// that every sounded member BOUNDS its span, so an open-air death is unrepresentable. That proof
// holds for the members a span currently states, and `nextSoundingPerString` was deleted with the
// conjunct — but the RESTRIKE CHAIN is the population it does not cover, and the third section is
// that population pinned: a same-grip restrike is the span CONTINUING (rule 8), so a merged chain
// reaches the LAST strike's rings and every earlier strike dies strictly inside its own span. That
// population is what settled the law on the COVERED comparison rather than exact-death: rule 11's
// "the close is the minimum" parenthetical was a proof about the members a span currently states,
// and renewal is how a replaced ring escapes it. The signed ruling — "hide ALL tails except the
// explicit exceptions" — prices exactly these interiors as hidden, the chug chain its headline.
TEST_CASE("Only a ring outliving its span keeps its ribbon", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a ring dying in open air inside the span is covered and hides")
    {
        // Four beats of rails over a ring that stops at beat four. The rails do not claim the
        // ring's length — they state the GRIP's tenure, which runs on — so hiding the ribbon is
        // the ruling's price ("hide ALL tails except the explicit exceptions"), and the caret and
        // Alt are where the beat-four death remains readable.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{3}),
            note(at(1, 2), 2, Fraction{7, 2}, 7),
        };

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — the whole three beats, because
        // the ring passes the partner's head by two and nothing stands after it.
        CHECK(shown[0] == Fraction{3});
        // The partner outlives the span by half a beat, so no verdict is passed on it at all —
        // the in-case control that the law is judging covered rings, not everything it sees.
        CHECK(shown[1] == Fraction{7, 2});
        CHECK_FALSE(hidden[1]);
    }

    SECTION("the same ring reaching the span's own close is hidden")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 2), 2, Fraction{3}, 7),
        };

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — four whole beats, the ring
        // passing the partner's head by three with nothing after it.
        CHECK(shown[0] == Fraction{4});
    }

    SECTION("a restruck string's first ring dies inside the merged span and hides with it")
    {
        // The slow restrike: the same chord struck at beats one and three, with a pluck between
        // them. Under grip-tenure derivation this is ONE span — the restatement of the same grip
        // continues it — reaching the close the SECOND strike's rings share, so the first strike's
        // rings die two beats inside their own span and the covered comparison takes them with
        // everything else: the whole chain rests ribbonless, the repeat heads and the rails
        // stating the rhythm the ribbons no longer duplicate at distance.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 1), 2, Fraction{2}, 7),
            note(at(1, 2), 3, Fraction{3}, 9),
            note(at(1, 3), 1, Fraction{2}),
            note(at(1, 3), 2, Fraction{2}, 7),
        };

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 5);
        REQUIRE(hidden.size() == 5);
        // The founding strike's rings die at the restrike, two beats inside the span: covered,
        // and the board rests them where the exact-death comparison passed no verdict at all.
        CHECK(hidden[0]);
        CHECK(hidden[1]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — rule 1's margin-trimmed stub
        // against the restrike two beats on, which is the bare picture to the tick.
        CHECK(shown[0] == presentedSustains(saved, map)[0]);
        CHECK(presentedSustains(saved, map)[0] == Fraction{7, 4});
        // The restrike reaches the close and is rested too, and so is the lone pluck between the
        // two strikes: both die where the span does.
        CHECK(hidden[2]);
        CHECK(hidden[3]);
        CHECK(hidden[4]);
        // Nothing is struck after the restrike, so rule 1 binds it nowhere and its rested ribbon
        // is the notated two beats.
        CHECK(shown[3] == Fraction{2});
    }
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
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // A four-beat ring reaching the span's close, and one head inside it whose distance from the
    // ring's end is the only thing that moves.
    const auto figure = [](const GridPosition head) {
        return std::vector<ChartNote>{
            note(at(1, 1), 1, Fraction{4}),
            note(head, 2, Fraction{1, 4}, 7),
        };
    };

    SECTION("a head the ring clears by more than the margin is hidden")
    {
        const std::vector<ChartNote> saved = figure(at(1, 4, Fraction{1, 2}));
        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
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
        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        CHECK(hidden[0]);
        // Rested, not shortened, and the amendment is what makes that readable in the value: the
        // presented ribbon IS the picture with no furniture, four whole beats, because the ring
        // passes that head by a quarter beat.
        CHECK(shown[0] == presentedSustains(saved, map)[0]);
        CHECK(presentedSustains(saved, map)[0] == Fraction{4});
    }
}

// SCOPE, and it is one-sided now because the other side is gone. The picking hand and a
// silently-held finger were never MEMBERS of what a grip states — a grip states where the FRETTING
// hand is, so a tap says nothing about whether that hand is still down — and they were also not
// CROSSING heads, on the side the 2026-09-04 ruling deleted. What survives is the membership half:
// a tap's own ring is a member of nothing and no span can take it, which is the one place this law
// moves ink UP.
TEST_CASE("A tap and a silent hold are no members of what a span states", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // Two rings both reaching the span's close at beat five, and the second one's ATTACK is the
    // only variable in the pair.
    const auto figure = [](const NoteAttack attack) {
        std::vector<ChartNote> saved{
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 2), 3, Fraction{3}, 9),
        };
        saved[1].attack = attack;
        return saved;
    };

    SECTION("a picked partner reaching the same close is hidden with it")
    {
        const std::vector<ChartNote> saved = figure(NoteAttack::Pick);
        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
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
        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        // The tap reaches the very close its picked twin above is hidden for, and NO verdict is
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

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 2);
        REQUIRE(hidden.size() == 2);
        // The neighbour dies at the span's close and the board rests it; the hold beside it
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
TEST_CASE("A stroke shares one tail verdict", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // A co-struck pair under one figure, with a head between them. Only the SECOND member's ring
    // length moves.
    const auto strum = [](const Fraction partner_ring) {
        return std::vector<ChartNote>{
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 1), 2, partner_ring, 7),
            note(at(1, 2), 3, Fraction{1}, 9),
        };
    };

    SECTION("both members reaching the close are hidden together")
    {
        const std::vector<ChartNote> saved = strum(Fraction{4});
        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // The stroke's ONE verdict, and since the execution-form amendment the verdict is the only
        // place it can be read: the law empties nothing now, so the presented rings say nothing
        // about whether the two members were judged together.
        CHECK(hidden[0]);
        CHECK(hidden[1]);
        // Rested, not shortened: both rings pass the head a beat in and nothing binds them after,
        // so each presents its notated four beats — the rules-1-to-4 form.
        CHECK(shown[0] == Fraction{4});
        CHECK(shown[1] == Fraction{4});
    }

    SECTION("one member leaving the span keeps its partner's ribbon too")
    {
        const std::vector<ChartNote> saved = strum(Fraction{9, 2});
        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
        REQUIRE(shown.size() == 3);
        // The AT-CLOSE member would be hidden on its own and the leaving one would not, which is
        // the inverted chord: a ribbon on the string that stopped and none on the one still
        // sounding.
        CHECK(shown[0] == Fraction{4});
        CHECK(shown[1] == Fraction{9, 2});
        CHECK_FALSE(hidden[0]);
        CHECK_FALSE(hidden[1]);
    }
}

// PRESENCE — nothing of its own. A ring carrying a sustain technique, or one whose string a later
// strike takes over, always shows its presence: the figure states where the hand IS, and it has no
// vocabulary for what the string is DOING nor for a TRANSFER of the sound.
TEST_CASE("A ring that states something of its own is never hidden", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

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
        const std::vector<Fraction> gliding = underSpans(marked, shapes, map);
        const std::vector<Fraction> planted = underSpans(plain, shapes, map);
        const std::vector<bool> gliding_hidden = hiddenUnderSpans(marked, shapes, map);
        const std::vector<bool> planted_hidden = hiddenUnderSpans(plain, shapes, map);

        REQUIRE(gliding.size() == 2);
        REQUIRE(planted.size() == 2);
        REQUIRE(gliding_hidden.size() == 2);
        REQUIRE(planted_hidden.size() == 2);
        CHECK(gliding[0] == Fraction{4});
        CHECK_FALSE(gliding_hidden[0]);
        // The control differs by that one keyframe and by nothing else — and since the
        // execution-form amendment the difference is the VERDICT, not the length. The law empties
        // nothing now, so the planted ring presents the same rules-1-to-4 four beats (rule 1 lets
        // it pass the partner's head, and nothing binds it after); what PRESENCE buys the glide is
        // that the board never rests its ribbon.
        CHECK(planted[0] == Fraction{4});
        CHECK(planted_hidden[0]);
    }

    SECTION("a handover keeps its ribbon at rest where a natural death does not")
    {
        // The discriminating pair the handover exists for: identical rings dying at the SPAN'S OWN
        // CLOSE, differing only in the successor's STORED claim. Duration cannot tell a transfer
        // from a release, which is why PRESENCE reads the claim rather than the length.
        //
        // The span closes at beat three, where both rings die, so the own-span comparison is
        // satisfied on both sides and the claim is the only thing left to decide the verdict. (The
        // outer span reaching beat five would not do: with the claim gone, the ring would still be
        // dying two beats inside its span and both halves would draw for the wrong reason.)
        const std::vector<ChartShape> closing = {
            ChartShape{.position = at(1, 1), .sustain = Fraction{2}},
        };
        const auto figure = [](const ChartNote& closer) {
            return std::vector<ChartNote>{
                note(at(1, 1), 1, Fraction{2}, 5),
                note(at(1, 2), 2, Fraction{1}, 7),
                closer,
            };
        };
        const std::vector<ChartNote> handed = figure(connected(at(1, 3), 1, Fraction{1}, 3));
        const std::vector<ChartNote> released = figure(note(at(1, 3), 1, Fraction{1}, 3));

        const std::vector<Fraction> junction = underSpans(handed, closing, map);
        const std::vector<bool> junction_hidden = hiddenUnderSpans(handed, closing, map);
        const std::vector<Fraction> death = underSpans(released, closing, map);
        const std::vector<bool> death_hidden = hiddenUnderSpans(released, closing, map);

        REQUIRE(junction.size() == 3);
        REQUIRE(death.size() == 3);
        // The handover keeps the ring, and rule 1 then binds it at the successor's own head — the
        // ordinary trim, applied to the ring the chart states.
        CHECK_FALSE(junction_hidden[0]);
        CHECK(junction[0] == Fraction{7, 4});
        // The natural death dies at its span's close and states nothing of its own, so the board
        // rests it. THE EXECUTION-FORM AMENDMENT is at its sharpest here: the verdict no longer
        // empties the tail, so the released ring presents the very same rules-1-to-4 trim as the
        // handover — 7/4, rule 1's margin short of the successor's head — and the whole of what
        // PRESENCE buys is the verdict.
        CHECK(death_hidden[0]);
        CHECK(death[0] == Fraction{7, 4});
        CHECK(death[0] == junction[0]);
        // The partner dies at that same close and is hidden either way, so neither answer above is
        // the law simply doing nothing.
        CHECK(junction_hidden[1]);
        CHECK(death_hidden[1]);
    }
}

// VERDICT-ONLY, AND LAST (the execution-form amendment, user ruling 2026-09-03). The law judges the
// tails rules 1 through 4 left standing — it empties none of them any more — and skips every tail
// those rules already emptied, so a zero from rule 3 or rule 4 never enters the hidden set, which
// is what keeps a staccato eighth and a dead chug out of the hold channel's span extension.
TEST_CASE("Emptiness the presentation rules own never enters the hidden set", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a dead member is emptied by rule 4, and its partner is judged alone")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 1), 2, Fraction{4}, 7),
            note(at(1, 2), 3, Fraction{4}, 9),
        };
        saved[1].dead = true;

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

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

    SECTION("a sub-quarter effect-free member is dropped by rule 3, never hidden")
    {
        // Eighth-note plucks that cross nothing: rule 3 drops both, in a figure the law IS live in
        // — the long member above them is hidden, so nothing here passes by the law being inert.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 2, Fraction{4}, 7),
            note(at(1, 3), 1, Fraction{1, 2}, 5),
            note(at(1, 3, Fraction{1, 2}), 1, Fraction{1, 2}, 3),
        };

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 3);
        CHECK(hidden[0]);
        CHECK(shown[1] == Fraction{});
        CHECK(shown[2] == Fraction{});
        CHECK_FALSE(hidden[1]);
        CHECK_FALSE(hidden[2]);
        // And the same figure with no furniture draws the same two zeros, which is what "the law
        // buys length nowhere" means.
        CHECK(presentedSustains(saved, map)[1] == Fraction{});
    }
}

// A RING OUTLIVING ITS OWN SPAN is never touched: past the close the ring is LEAVING the grip it
// was struck in, and that is exactly the news the ruling wants inked. Pinned against members of
// the same span that ARE hidden, so neither answer can be the law doing nothing.
TEST_CASE("A ring outliving its own span is never hidden", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a mid-span member's outliving ring draws while its neighbours hide")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}, 0),
            note(at(1, 2), 2, Fraction{4}, 7),
            note(at(1, 3), 3, Fraction{2}, 9),
        };

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 3);
        REQUIRE(hidden.size() == 3);
        // Three rings, one span: the two that die at its close are rested, and the one that
        // outlives it by a beat is not. That is the only difference between them — and since the
        // execution-form amendment it is a difference in the VERDICT alone, the law emptying no
        // tail, so all three present exactly the bare rules-1-to-4 picture.
        CHECK(hidden[0]);
        CHECK_FALSE(hidden[1]);
        CHECK(hidden[2]);
        CHECK(shown == presentedSustains(saved, map));
        // Concretely: the two long rings pass every head after them and the late one has none in
        // front of it, so rule 1 binds nothing in this figure.
        CHECK(shown[0] == Fraction{4});
        CHECK(shown[1] == Fraction{4});
        CHECK(shown[2] == Fraction{2});
    }

    SECTION("a span-final outliving ring draws whole across the span that follows")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1}, 0),
            note(at(1, 3), 2, Fraction{4}, 7),
            note(at(2, 1), 3, Fraction{1}, 9),
            note(at(2, 1), 4, Fraction{1}, 5),
        };

        const std::vector<Fraction> shown = underSpans(saved, shapes, map);
        const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

        REQUIRE(shown.size() == 4);
        REQUIRE(hidden.size() == 4);
        // The early filler dies inside the span and is covered — the in-section contrast, and
        // since the execution-form amendment the verdict is what carries it: the law empties no
        // tail, so the filler's one-beat ring presents whole (it clears the margin before the
        // strum two beats on, so rule 1 does not bind it either).
        CHECK(hidden[0]);
        CHECK_FALSE(hidden[1]);
        CHECK(shown[0] == Fraction{1});
        CHECK(shown[1] == Fraction{4});
    }
}

// EVERY ring whose death closes a span ends exactly AT that close, and the law takes them all —
// the member that only just reaches the closing statement included, which is the 2026-09-04
// reversal applied to the whole co-terminating family at once.
TEST_CASE("Every ring ending at the span's close is hidden", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // The span's MUSICAL CLOSE is the closing statement at the next downbeat, exactly as the
    // derivation stores it; the margin its rails keep from that head is the projection's.
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{4}, 0),
        note(at(1, 2), 2, Fraction{3}, 0),
        note(at(1, 3), 3, Fraction{2}, 4),
        note(at(2, 1), 2, Fraction{1}, 5),
    };

    const std::vector<Fraction> shown = underSpans(saved, shapes, map);
    const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);

    REQUIRE(shown.size() == 4);
    REQUIRE(hidden.size() == 4);
    CHECK(hidden[0]);
    CHECK(hidden[1]);
    // The execution-form amendment: the verdict no longer empties the tail; hidden means the board
    // rests it, and the value is the rules-1-to-4 form. Every co-terminating ring here binds on the
    // closing statement at the next downbeat, so each presents its own distance to that head less
    // the quarter-beat margin.
    CHECK(shown[0] == Fraction{15, 4});
    CHECK(shown[1] == Fraction{11, 4});
    // The last member reaches the closing statement itself. Under the deleted CROSSING conjunct it
    // cleared no head and drew its own margin-trimmed rhythm; it is now one sentence with its
    // neighbours — rested, not shortened, and the bare picture beside it is that same rhythm to
    // the tick, which is exactly what the amendment restored to the ribbon.
    CHECK(shown[2] == Fraction{7, 4});
    CHECK(hidden[2]);
    CHECK(shown[2] == presentedSustains(saved, map)[2]);
    CHECK(presentedSustains(saved, map)[2] == Fraction{7, 4});
    // The closing statement's own ring runs a beat PAST the close, so it is leaving rather than
    // dying there and keeps its whole ring.
    CHECK(shown[3] == Fraction{1});
    CHECK_FALSE(hidden[3]);
}

// THE HOLD CHANNEL reads the verdict, not the tail: a hidden member holds its OWN STORED RING,
// never the ribbon the presentation rules sized for it.
//
// THE HOLD IS THE TENURE (user sighting 2026-09-03, overruling the brief own-ring reading the
// covered comparison shipped with): while the grip is held the board pins what is held, so a
// hidden member is held to its span's reach — the restrike interior included, whose own ring the
// restrike replaced without the finger ever lifting. In this lone figure the two answers
// coincide (the ring dies at the close), and one load-bearing pin is left: the rules-1-to-4 margin
// trim, which the execution-form amendment turned from a discarded intermediate into the ribbon
// the member actually presents — so it is now the number the extension is likeliest to pick up.
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
    const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
    const std::vector<Fraction> shown = underSpans(saved, shapes, map);

    REQUIRE(holds.size() == 2);
    REQUIRE(hidden.size() == 2);
    REQUIRE(shown.size() == 2);
    CHECK(hidden[0]);
    // The execution-form amendment: the verdict no longer empties the tail; hidden means the board
    // rests it, and the value is the rules-1-to-4 form — 15/4, a quarter beat short of the closing
    // statement's head.
    CHECK(shown[0] == Fraction{15, 4});
    CHECK(shown[0] == presentedSustains(saved, map)[0]);
    // The span's reach from its onset: four beats. NOT the 15/4 the margin trim leaves standing on
    // the ribbon. (The presented zero this used to be pinned against is gone with the amendment,
    // so the trim is the whole of what the extension must not pick up.)
    CHECK(holds[0] == Fraction{4});
    CHECK(holds[0] != Fraction{15, 4});
    CHECK(presentedSustains(saved, map)[0] == Fraction{15, 4});
    // The closing statement outlives the span, so it is not hidden and holds exactly what it
    // presents.
    CHECK_FALSE(hidden[1]);
    CHECK(holds[1] == Fraction{1});
}

// THE DERIVED FIGURES. Everything above states its spans; these state only NOTES and let the
// derivation answer, which is the only way to pin what the law does to the shapes real material
// implies — and, since the grip-tenure rebuild, the only way to see that the spans themselves are
// no longer the ones the old machine built.
TEST_CASE("A derived box span takes exactly the rings it covers", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a box span's uneven rings keep the tails presentation gave them")
    {
        // One strum, two unequal rings, nothing carried or claimed and no tap: the shape sounds
        // whole, so the span arrives as a BOX and its reach is the SHORTER member's ring. THE
        // STROKE ATOM is what saves both tails — the short member dies at the close and the long
        // one leaves, and a chord showing a ribbon on the string that stopped and none on the
        // string still sounding is a picture no strum makes.
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
        CHECK_FALSE(figure.hidden[0]);
        CHECK_FALSE(figure.hidden[1]);
    }

    SECTION("a quarter-note chug chain goes ribbonless end to end")
    {
        // THE RESTRIKE CHAIN, derived: the same grip struck on three consecutive beats is ONE span
        // (rule 8 — a restatement of the same grip continues it), reaching the LAST strike's rings
        // at beat four. The earlier strikes' rings die at their own restrikes, well inside that
        // span — the population that falsified rule 11's "close is the minimum" proof — and the
        // covered comparison takes every one of them, which is exactly the consequence
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

// A DRY ARPEGGIO — the stepped look the retired staircase used to invent — rests whole under its
// span: every step's ring ends at its own next head, well inside the span, and the covered
// comparison makes no distinction between dying inside and dying at the close (the signed ruling:
// "hide ALL tails except the explicit exceptions"). At distance the rhythm those stubs duplicate
// is the heads' own, so the bracket, the rails, and the hold-pinned heads state the tenure; the
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

    const std::vector<Fraction> shown = underSpans(saved, shapes, map);
    const std::vector<bool> hidden = hiddenUnderSpans(saved, shapes, map);
    const std::vector<Fraction> bare = presentedSustains(saved, map);
    const std::vector<Fraction> holds = holdsUnderSpans(saved, shapes, map);

    REQUIRE(shown.size() == 4);
    REQUIRE(hidden.size() == 4);
    REQUIRE(holds.size() == 4);
    REQUIRE(bare.size() == 4);
    for (std::size_t index = 0; index < 4; ++index)
    {
        CHECK(hidden[index]);
        // The execution-form amendment: the verdict no longer empties the tail; hidden means the
        // board rests it, and the value is the rules-1-to-4 form — the bare picture beside it,
        // step for step.
        CHECK(shown[index] == bare[index]);
        // The span's reach from each step's own onset — every finger stays down to the one close,
        // which is the discrimination against the one-beat stored rings and, on the three steps
        // rule 1 trims, against the presented ribbons the amendment restored.
        CHECK(holds[index] == Fraction{static_cast<int>(4 - index)});
    }
    // That form concretely: each step but the last binds on the next head a beat away and trims to
    // the margin, and the last rings its whole beat.
    CHECK(bare[0] == Fraction{3, 4});
    CHECK(bare[3] == Fraction{1});
}

} // namespace rock_hero::common::core
