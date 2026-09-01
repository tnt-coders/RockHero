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
// class those spans arrive as, the bracket re-read of the covered rings, and the presentation
// rules over the result — which is exactly the order \ref chartResolutions runs. `presented` is
// therefore the stream both surfaces draw. The cases that turn on a span's own facts — its class
// above all — use this rather than handing in a shape, because a case stating those facts itself
// would be stating the very things the rule is a question about.
struct BracketFigure
{
    std::vector<ChartNote> presented;
    std::vector<ChartShape> shapes;
    std::vector<bool> arrivals;
};

[[nodiscard]] BracketFigure bracketFigure(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    BracketFigure figure;
    figure.shapes =
        deriveChartShapes(saved, chartClaimedStops(chartConnections(saved, tempo_map)), tempo_map)
            .shapes;
    figure.arrivals = chartShapeArrivals(saved, figure.shapes, tempo_map);
    std::vector<ChartNote> staircase = saved;
    clipArpeggioTails(staircase, figure.shapes, figure.arrivals, tempo_map);
    figure.presented = presentedChartNotes(staircase, tempo_map);
    return figure;
}

// The presented sustains a stated ARPEGGIO span leaves, for the cases that state the span
// themselves. One span, arriving as a bracket, is the whole fixture those cases need; the re-read
// runs before presentation, as \ref chartResolutions runs it.
[[nodiscard]] std::vector<Fraction> clippedUnderBracket(
    const std::vector<ChartNote>& saved, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    std::vector<ChartNote> staircase = saved;
    clipArpeggioTails(staircase, shapes, std::vector<bool>(shapes.size(), true), tempo_map);
    std::vector<Fraction> sustains;
    for (const ChartNote& note : presentedChartNotes(staircase, tempo_map))
    {
        sustains.push_back(note.sustain);
    }
    return sustains;
}

// The presented sustains alone, which is what nearly every case below is about.
[[nodiscard]] std::vector<Fraction> presentedSustains(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    std::vector<Fraction> sustains;
    for (const ChartNote& presented : presentedChartNotes(saved, tempo_map))
    {
        sustains.push_back(presented.sustain);
    }
    return sustains;
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
    }

    SECTION("a vibrato statement that repeats the standing state holds nothing open")
    {
        // Every channel is read against the value the note OPENS with, so a shake restated at an
        // instant it already had says nothing new and the margin trim stands.
        saved[0].vibrato = VibratoState::Narrow;
        saved[0].keyframes = {Keyframe{.offset = Fraction{7, 4}, .vibrato = VibratoState::Narrow}};

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

    const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
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

    const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
    const std::vector<Fraction> holds = chartHolds(presented, shapes, map);
    REQUIRE(holds.size() == saved.size());
    // Every chug is a half-beat effect-free ring, so rule 3 presents no tail on any of them.
    for (const ChartNote& shown : presented)
    {
        CHECK(shown.sustain == Fraction{});
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

// The three cases the span-implied hold deliberately does NOT claim.
TEST_CASE("Presented tails, dead groups and singles hold what they show", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a member that presents a tail keeps its own hold")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
            note(at(2, 1), 3, Fraction{1}, 3),
        };

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        const std::vector<Fraction> holds = chartHolds(presented, shapes, map);
        REQUIRE(holds.size() == saved.size());
        // One member reaches the kept-sustain bound, so rule 3 keeps the whole group's tails and
        // the span has nothing to extend: each member holds exactly what it draws.
        CHECK(holds[0] == Fraction{2});
        CHECK(holds[1] == Fraction{1, 2});
        CHECK(holds[2] == Fraction{1});
    }

    SECTION("an all-dead group is choked rather than held")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1), 2, Fraction{1, 2}, 7),
            note(at(2, 1), 3, Fraction{1}, 3),
        };
        saved[0].dead = true;
        saved[1].dead = true;

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        const std::vector<Fraction> holds = chartHolds(presented, shapes, map);
        REQUIRE(holds.size() == saved.size());
        CHECK(holds[0] == Fraction{});
        CHECK(holds[1] == Fraction{});
        CHECK(holds[2] == Fraction{1});
    }

    SECTION("a single note under a span is not a strum and holds only its own tail")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(2, 1), 3, Fraction{1}, 3),
        };

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        const std::vector<Fraction> holds = chartHolds(presented, shapes, map);
        REQUIRE(holds.size() == saved.size());
        CHECK(holds[0] == Fraction{});
        CHECK(holds[1] == Fraction{1});
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        const std::vector<Fraction> holds = chartHolds(presented, shapes, map);
        REQUIRE(holds.size() == 2);
        // A quarter beat of span A is left, and that remainder is the hold. A cursor that
        // remembered only span B would find no cover at all and hold nothing.
        CHECK(holds[0] == Fraction{1, 4});
        CHECK(holds[1] == Fraction{1, 4});

        // Listing order must not matter either: the same two spans the other way round give the
        // same answer, which a last-writer-wins cursor could not promise.
        const std::vector<ChartShape> reversed = {shapes[1], shapes[0]};
        const std::vector<Fraction> held_reversed = chartHolds(presented, reversed, map);
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

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        const std::vector<Fraction> holds = chartHolds(presented, shapes, map);
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

// THE BRACKET LAW: a bracket states the hold, so its members' ribbons stop restating it and read
// RHYTHM instead — each running from its own head to the NEXT ONSET and no further. What that
// replaced hid those ribbons outright, so every figure below used to draw nothing at all where it
// now draws a step.
//
// The span is STATED here rather than derived, because these cases are about the clip's arithmetic
// and a derived figure would be stating the class as well. The cases that turn on the class use the
// derivation instead (below).
TEST_CASE("A bracket clips its members' tails at the next onset", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // One span across the whole measure, arriving as a bracket.
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a picked arpeggio draws a staircase")
    {
        // Three let-ring plucks, one per beat on three strings, every ring notated to the figure's
        // own boundary at beat five — the honest let-ring texture: rings crossing each other and
        // ending together where the span ends. Rule 1 leaves all three whole — every ring PASSES
        // the onsets after it, which is the deliberate hold — so the un-re-read picture is three
        // ribbons lying across each other's heads.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 2), 2, Fraction{3}, 7),
            note(at(1, 3), 3, Fraction{2}, 9),
        };

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 3);
        // Each step ends one margin before the next pluck: a beat, less the 4/4 quarter beat.
        CHECK(clipped[0] == Fraction{3, 4});
        CHECK(clipped[1] == Fraction{3, 4});
        // THE DISCRIMINATOR against the whole rule doing nothing: the last pluck has no onset
        // after it, so nothing re-reads it and it keeps its whole ring. The clip is the next ONSET
        // and not the span's edge.
        CHECK(clipped[2] == Fraction{2});
        // And against rule 1 alone, which left every one of them whole.
        CHECK(
            presentedSustains(saved, map) ==
            std::vector<Fraction>{Fraction{4}, Fraction{3}, Fraction{2}});
    }

    SECTION("a mid-span long hold shows its tail, clipped")
    {
        // A four-beat ring under the bracket with one strum after it. The ring covers the span
        // exactly, which is precisely the case the retired rule hid whole.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 3), 2, Fraction{2}, 7),
        };

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 2);
        // Two beats to the next onset, less the margin.
        CHECK(clipped[0] == Fraction{7, 4});
        CHECK(clipped[1] == Fraction{2});
    }
}

// THE MOTIVATING ODDITY (user, 2026-09-01): the LAST member of a bracketed figure held a long ring
// and showed no tail whatever, because its ring ended inside the span and the bracket owned every
// bit of that ink. Nothing follows it to clip against, so under the bracket law it simply draws.
TEST_CASE("A span-final long hold shows its whole tail", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // The second member is struck on beat three and rings two beats — to the span's own end, to the
    // tick. Under the retired rule that was the definition of ink the mark owned whole.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{1}),
        note(at(1, 3), 2, Fraction{2}, 7),
    };

    const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

    REQUIRE(clipped.size() == 2);
    // The opener already cleared the next onset by more than a margin, so nothing moves it either.
    CHECK(clipped[0] == Fraction{1});
    CHECK(clipped[1] == Fraction{2});
}

// THE SIGHTED FIGURE (user, 2026-09-01): a real let-ring texture opens with a STRUMMED PAIR whose
// rings the later plucks accumulate over, all ending together at the figure's boundary. The
// derivation puts the strum's own onset under a small statement-founded BOX span and carries its
// rings into the arpeggio span the growth split opens — so a clip keyed on the span over the
// member's ONSET read the box and left the founding rings drawn whole across the bracket's heads.
// The law keys on the HEAD BEING CROSSED instead, and this figure is the regression pin.
TEST_CASE("A founding strum's rings clip under the bracket that follows", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // Two strings strummed at beat one; single plucks on new strings at beats two and three; every
    // ring runs to beat five — rings crossing each other, ending at a later span boundary.
    const BracketFigure figure = bracketFigure(
        {
            note(at(1, 1), 1, Fraction{4}, 0),
            note(at(1, 1), 2, Fraction{4}, 7),
            note(at(1, 2), 3, Fraction{3}, 9),
            note(at(1, 3), 4, Fraction{2}, 5),
        },
        map);

    // The association trap this case exists for: the span covering the STRUM'S onset is a box —
    // the strum sounds its whole two-string shape — and only the growth split's successor
    // classifies arpeggio. Keyed on the onset's own span, the strum's rings drew whole.
    REQUIRE(figure.shapes.size() >= 2);
    REQUIRE(figure.arrivals.size() == figure.shapes.size());
    CHECK_FALSE(figure.arrivals[0]);
    CHECK(figure.arrivals[1]);
    REQUIRE(figure.presented.size() == 4);
    // The strummed pair steps down at the first pluck's head like any other member: one beat,
    // less the 4/4 quarter-beat margin.
    CHECK(figure.presented[0].sustain == Fraction{3, 4});
    CHECK(figure.presented[1].sustain == Fraction{3, 4});
    // The plucks staircase on: the middle one to the next head, the last one whole.
    CHECK(figure.presented[2].sustain == Fraction{3, 4});
    CHECK(figure.presented[3].sustain == Fraction{2});
}

// THE COMPOSE (user ruling 2026-09-01): the re-read runs BEFORE the presentation rules, so every
// standard tail rule judges the staircase ring exactly as it judges an equal stored one — and a
// sub-quarter staircase step therefore draws NOTHING, because rule 3 drops an effect-free ring
// under the kept-sustain bound wherever it comes from. The sighting this pins: stubs on sub-1/4
// figures inside spans where the standard rules draw no tails at all.
TEST_CASE("A sub-quarter staircase draws no tails", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // Eighth-note let-ring plucks, every ring crossing the heads after it to beat three. The
    // re-read turns each ring into its half-beat rhythm, under the 4/4 kept-sustain bound of one
    // beat.
    const std::vector<ChartNote> crossing = {
        note(at(1, 1), 1, Fraction{2}, 0),
        note(at(1, 1, Fraction{1, 2}), 2, Fraction{3, 2}, 7),
        note(at(1, 2), 3, Fraction{1}, 9),
        note(at(1, 2, Fraction{1, 2}), 4, Fraction{1, 2}, 5),
    };

    const std::vector<Fraction> clipped = clippedUnderBracket(crossing, shapes, map);

    REQUIRE(clipped.size() == 4);
    // No member earns a tail: each re-read ring is a half beat, effect-free, passing nothing.
    CHECK(clipped[0] == Fraction{});
    CHECK(clipped[1] == Fraction{});
    CHECK(clipped[2] == Fraction{});
    CHECK(clipped[3] == Fraction{});

    // The compose stated as the ruling states it: the in-span picture equals the out-of-span
    // picture of the figure whose stored rings ARE the staircase — same onsets, half-beat rings,
    // no span anywhere.
    const std::vector<ChartNote> equal_rings = {
        note(at(1, 1), 1, Fraction{1, 2}, 0),
        note(at(1, 1, Fraction{1, 2}), 2, Fraction{1, 2}, 7),
        note(at(1, 2), 3, Fraction{1, 2}, 9),
        note(at(1, 2, Fraction{1, 2}), 4, Fraction{1, 2}, 5),
    };
    CHECK(clipped == presentedSustains(equal_rings, map));
}

// THE PAST-SPAN-END EXCEPTION (user ruling 2026-09-01): a member whose ring extends PAST the end
// of its span always shows its tail — the ring outliving the held shape IS the information, so the
// staircase never takes it and only the standard rules apply. Both variants pin against a partner
// in the same figure that IS clipped, so neither can pass by the clip doing nothing.
TEST_CASE("A ring outliving its span is exempt from the staircase", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a mid-span member's outliving ring draws whole across the heads after it")
    {
        // The second member is struck at beat two and rings to beat six — one beat past the
        // span's own end — while its neighbours stay inside the figure.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}, 0),
            note(at(1, 2), 2, Fraction{4}, 7),
            note(at(1, 3), 3, Fraction{1}, 9),
        };

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 3);
        // The in-span partner steps down at the next head as ever.
        CHECK(clipped[0] == Fraction{3, 4});
        // The outliving ring is the exception: whole, straight across the beat-three head.
        CHECK(clipped[1] == Fraction{4});
        CHECK(clipped[2] == Fraction{1});
    }

    SECTION("a span-final outliving ring draws whole across the figure that follows")
    {
        // The final member rings two beats past the span's end, and the NEXT figure's strum stands
        // at beat five — under the span's closed edge, so a non-exempt ring would step down to it.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1}, 0),
            note(at(1, 3), 2, Fraction{4}, 7),
            note(at(2, 1), 3, Fraction{1}, 9),
            note(at(2, 1), 4, Fraction{1}, 5),
        };

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 4);
        CHECK(clipped[0] == Fraction{1});
        // Whole, not the seven-quarters a staircase step to the beat-five strum would leave.
        CHECK(clipped[1] == Fraction{4});
        CHECK(clipped[1] != Fraction{7, 4});
    }
}

// The clip is a MEMBERSHIP rule, and its two exclusions are the only ones: the picking hand is a
// member of nothing, and a silently-held stop has no ribbon to clip. Both are asserted against a
// partner in the same figure that IS clipped, so neither can pass by nothing being clipped at all.
TEST_CASE("The bracket clips fretting-hand soundings and nothing else", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a right-hand onset is a member of nothing")
    {
        // A tap sounding at the same instant as a member of the shape, ringing exactly as long.
        // The fretting hand's ribbon is the bracket's to clip; the tap's is its own.
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 1), 3, Fraction{4}, 9),
            note(at(1, 2), 2, Fraction{4}, 7),
        };
        saved[1].attack = NoteAttack::Tap;

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 3);
        CHECK(clipped[0] == Fraction{3, 4});
        CHECK(clipped[1] == Fraction{4});
    }

    SECTION("a silently-held stop binds nothing in front of it")
    {
        // A held finger between two plucks. It draws no head, so a ribbon ending at its instant
        // would end in empty space — the scan steps over it exactly as rule 1 does.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            heldStop(at(1, 2), 2, 7),
            note(at(1, 3), 3, Fraction{4}, 9),
        };

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 3);
        // Two beats to the sounding onset, less the margin. Had the hold bound, this would be one
        // beat less the margin instead, so the case discriminates sharply.
        CHECK(clipped[0] == Fraction{7, 4});
        CHECK(clipped[0] != Fraction{1} - minimumSustainDistanceBeats(4));
        // The hold itself has no tail to take.
        CHECK(clipped[1] == Fraction{});
    }
}

// The ORDINARY rules still run, and they run first: the clip only ever shortens what presentation
// already decided to draw, so a tail presentation dropped stays dropped and a payload it floored
// stays floored.
TEST_CASE("The bracket clips on top of the ordinary presentation rules", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };

    SECTION("a dead member presents no tail, so there is nothing to clip")
    {
        // Rule 4: a plain dead note rings nothing, so its tail is silence pretending to be sound.
        // Its live partner is clipped in the same figure, which is what keeps this discriminating.
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{4}),
            note(at(1, 1), 2, Fraction{4}, 7),
            note(at(1, 2), 3, Fraction{4}, 9),
        };
        saved[1].dead = true;

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 3);
        CHECK(clipped[0] == Fraction{3, 4});
        CHECK(clipped[1] == Fraction{});
    }

    SECTION("payload floors the clip exactly as it floors rule 1's trim")
    {
        // A member gliding to a new fret two beats in, with the next pluck one beat away. The
        // margin would cut the ribbon at three quarters of a beat and take the glide's arrival with
        // it, so rule 2's floor holds the tail open to the statement and stops there.
        const std::vector<ChartNote> saved = {
            travellingAt(note(at(1, 1), 1, Fraction{4}), {{Fraction{2}, 12}}),
            note(at(1, 2), 2, Fraction{4}, 7),
        };

        const std::vector<Fraction> clipped = clippedUnderBracket(saved, shapes, map);

        REQUIRE(clipped.size() == 2);
        CHECK(clipped[0] == Fraction{2});
        CHECK(clipped[0] != Fraction{3, 4});
    }
}

// THE BRACKET CLIPS; THE BOX DOES NOT. Ownership of the hold belongs to furniture drawn across the
// stretch its members arrive over, and only an arpeggio's bracket is. A chord box is drawn at an
// INSTANT and states a strum, so it says nothing about how long anything rings and the members'
// tails are simply their own.
TEST_CASE("A chord box clips none of its members' tails", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a box span's ringing members keep the tails presentation gave them")
    {
        // One strum, two unequal rings, nothing carried or claimed and no tap: the shape sounds
        // whole, so the span arrives as a BOX.
        const BracketFigure figure = bracketFigure(
            {
                note(at(1, 1), 1, Fraction{2}),
                note(at(1, 1), 2, Fraction{4}, 7),
            },
            map);

        REQUIRE(figure.shapes.size() == 1);
        REQUIRE(figure.arrivals.size() == 1);
        CHECK_FALSE(figure.arrivals[0]);
        // Both rings reach the kept-sustain bound, so both members earn the tails they keep, and
        // nothing after them clips anything.
        REQUIRE(figure.presented.size() == 2);
        CHECK(figure.presented[0].sustain == Fraction{2});
        CHECK(figure.presented[1].sustain == Fraction{4});
    }

    SECTION("a chug under a box still shows nothing, because presentation emptied it")
    {
        // The discrimination: what keeps a chugged riff clean under a box is rule 3's earning, not
        // anything span-scoped. No member reaches the kept-sustain bound and none carries a
        // technique, so the group presents no tail at all.
        const BracketFigure figure = bracketFigure(
            {
                note(at(1, 1), 1, Fraction{1, 4}),
                note(at(1, 1), 2, Fraction{1, 4}, 7),
            },
            map);

        REQUIRE(figure.shapes.size() == 1);
        CHECK_FALSE(figure.arrivals[0]);
        REQUIRE(figure.presented.size() == 2);
        CHECK(figure.presented[0].sustain == Fraction{});
        CHECK(figure.presented[1].sustain == Fraction{});
    }

    SECTION("a carried ring flips the class, and the bracket then clips the strum")
    {
        // The let-ring figure, and the control for the section above: a ring crossing the strum's
        // onset joins the posture and makes the span an ARPEGGIO, so the same two struck members
        // that would have kept their tails under a box come under the bracket's clip.
        const BracketFigure figure = bracketFigure(
            {
                note(at(1, 1), 2, Fraction{4}, 7),
                note(at(1, 3), 1, Fraction{2}),
                note(at(1, 3), 3, Fraction{2}, 9),
            },
            map);

        REQUIRE(figure.shapes.size() == 1);
        REQUIRE(figure.arrivals.size() == 1);
        CHECK(figure.arrivals[0]);
        REQUIRE(figure.presented.size() == 3);
        // THE DATING RULE (user ruling 2026-08-31) puts the span's front at the carried member's
        // own onset, so that member is under the bracket from its head and its ring stops at the
        // strum: two beats, less the margin. Under the retired rule all three drew nothing.
        CHECK(figure.presented[0].sustain == Fraction{7, 4});
        // The strum itself has no onset after it, so its members keep their whole presented rings.
        CHECK(figure.presented[1].sustain == Fraction{2});
        CHECK(figure.presented[2].sustain == Fraction{2});
    }
}

// A span COVERING A GLIDE is not a case of its own any more, and this is where that shows. Under
// the retired rule it carved out an exemption ([D2] amendment 1) because a standing mark and a
// travelling ribbon stopped saying the same thing; a clipped ribbon and a bracket never said the
// same thing to begin with, so the clip fires here like anywhere else and rule 2's payload floor —
// not a span flag — is what keeps the travel drawable. The figure is the user's: fretted members
// slide while open strings ring and are picked again underneath.
TEST_CASE("A span covering travel clips its members like any other", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // Two fretted members glide from beat one to the landing at beat three, ringing on to beat
    // five; two open strings ring through the whole figure, re-picked at beat two — strike into
    // strike, so their chains are continuous.
    const auto figure_of = [](const std::vector<std::pair<Fraction, int>>& path) {
        return std::vector<ChartNote>{
            travellingAt(note(at(1, 1), 1, Fraction{4}, 5), path),
            travellingAt(note(at(1, 1), 2, Fraction{4}, 7), path),
            note(at(1, 1), 3, Fraction{1}, 0),
            note(at(1, 1), 4, Fraction{1}, 0),
            note(at(1, 2), 3, Fraction{3}, 0),
            note(at(1, 2), 4, Fraction{3}, 0),
        };
    };

    const BracketFigure sliding = bracketFigure(figure_of({{Fraction{2}, 12}}), map);

    // Two spans that tile at the landing: the departing grip covering the glide, and the successor
    // the landed grip opens. The travel flag still derives, and the clip is simply not gated on it.
    REQUIRE(sliding.shapes.size() == 2);
    CHECK(sliding.shapes[0].covers_travel);
    CHECK(sliding.shapes[1].carry_opened);
    // The re-picked open strings sound part of the shape, so the covering span is an arpeggio.
    REQUIRE(sliding.arrivals.size() == 2);
    CHECK(sliding.arrivals[0]);
    REQUIRE(sliding.presented.size() == 6);
    // THE TRAVELLING MEMBERS: the next onset is a beat away and the margin would cut them at three
    // quarters of it, but the glide's arrival is two beats in, so the ribbon holds open to the
    // landing and stops exactly there. Ink to the arrival, and none past it.
    CHECK(sliding.presented[0].sustain == Fraction{2});
    CHECK(sliding.presented[1].sustain == Fraction{2});
    // The open strings' first rings are cut by their own re-pick under rule 1 already.
    CHECK(sliding.presented[2].sustain == Fraction{3, 4});
    // And the re-picked rings have no onset after them at all, so they draw straight through the
    // landing — the half of the figure the reader actually notices.
    CHECK(sliding.presented[4].sustain == Fraction{3});

    // The control, one channel apart: the same figure with the hand STILL states no travel, so
    // there is no payload to floor the clip and the fretted members stop at the margin.
    const BracketFigure still = bracketFigure(figure_of({}), map);

    REQUIRE(still.shapes.size() >= 1);
    CHECK_FALSE(still.shapes[0].covers_travel);
    REQUIRE(still.arrivals.size() >= 1);
    CHECK(still.arrivals[0]);
    REQUIRE(still.presented.size() == 6);
    CHECK(still.presented[0].sustain == Fraction{3, 4});
    CHECK(still.presented[1].sustain == Fraction{3, 4});
}

// THE N5 FIGURE: a lone ringing note FOLDS INTO a chord's posture and then glides under it. What it
// pins now is the floor doing the work the carve-out used to — the fold-in's own glide is on the
// tail the clip is shortening, so the tail keeps exactly the length that glide needs and no more.
TEST_CASE("A fold-in's own glide floors the clip on its tail", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // A note on string 3 rings from beat one; the chord on strings 1 and 2 is struck at beat two,
    // folding that ring into its posture. The carry HOLDS its stop across the fold-in slot and
    // departs after it, so it is a member of the grip and then travels beneath the span.
    const auto figure_of = [](const std::vector<std::pair<Fraction, int>>& path) {
        return std::vector<ChartNote>{
            travellingAt(note(at(1, 1), 3, Fraction{4}, 9), path),
            note(at(1, 2), 1, Fraction{3}, 5),
            note(at(1, 2), 2, Fraction{3}, 7),
        };
    };

    const BracketFigure gliding =
        bracketFigure(figure_of({{Fraction{2}, 9}, {Fraction{3}, 12}}), map);

    // The span DATES from the ringing note (THE ACCUMULATION LAW, 2026-08-31), so that ring is a
    // founding member and travels beneath its own span.
    REQUIRE(gliding.shapes.size() >= 1);
    CHECK(gliding.shapes[0].covers_travel);
    REQUIRE(gliding.arrivals.size() >= 1);
    CHECK(gliding.arrivals[0]);
    REQUIRE(gliding.presented.size() == 3);
    // The chord is struck a beat after the carry, so the margin alone would leave three quarters of
    // a beat. The departure three beats in is the information the floor protects, and the tail runs
    // to it. The equal-fret statement at two beats is a HOLD and floors nothing.
    CHECK(gliding.presented[0].sustain == Fraction{3});
    // The chord's own members have nothing after them to clip against.
    CHECK(gliding.presented[1].sustain == Fraction{3});
    CHECK(gliding.presented[2].sustain == Fraction{3});

    // The control, one keyframe apart: the same carry HOLDING its stop states no travel at all, so
    // nothing floors the clip and the ribbon stops at the margin before the chord.
    const BracketFigure planted = bracketFigure(figure_of({{Fraction{2}, 9}}), map);

    REQUIRE(planted.shapes.size() == 1);
    CHECK_FALSE(planted.shapes[0].covers_travel);
    REQUIRE(planted.arrivals.size() == 1);
    CHECK(planted.arrivals[0]);
    REQUIRE(planted.presented.size() == 3);
    CHECK(planted.presented[0].sustain == Fraction{3, 4});
}

} // namespace rock_hero::common::core
