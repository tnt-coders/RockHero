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

// A whole figure's suppression answer, DERIVED the way every reader gets it: the picture the notes
// present, the spans they imply, the class those spans arrive as, and the ink the spans own. The
// cases that turn on a span's own facts — its class, and whether it covers a glide — use this
// rather than handing in a shape, because a case stating those facts itself would be stating the
// very things the rule is a question about.
struct SuppressedFigure
{
    std::vector<ChartNote> presented;
    std::vector<ChartShape> shapes;
    std::vector<bool> arrivals;
    std::vector<bool> suppressed;
};

[[nodiscard]] SuppressedFigure suppressedFigure(
    const std::vector<ChartNote>& saved, const TempoMap& tempo_map)
{
    SuppressedFigure figure;
    figure.presented = presentedChartNotes(saved, tempo_map);
    figure.shapes =
        deriveChartShapes(saved, chartClaimedStops(chartConnections(saved, tempo_map)), tempo_map)
            .shapes;
    figure.arrivals = chartShapeArrivals(figure.presented, figure.shapes, tempo_map);
    figure.suppressed =
        chartSuppressedTails(figure.presented, figure.shapes, figure.arrivals, tempo_map);
    return figure;
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

// C3, the cover predicate: whether a member's covering span's ink owns its ring WHOLE. The span's
// extent is the minimum of its members' ring chains, so the mark over that stretch states exactly
// what each ribbon would state there — which is what makes hiding them honest rather than lossy.
//
// SUPPRESSION IS ALL-OR-NOTHING PER NOTE (user ruling 2026-08-30), and this case is its
// discriminator: the two members disagree under the two rules, and only the one still ringing past
// the bracket changes answer.
//
// The span is stated as an ARPEGGIO because that is the only class that owns ink at all: a bracket
// is drawn across the stretch its members arrive over, which is the stretch their tails would
// occupy, where a box is drawn at an instant. The box case has its own case below.
TEST_CASE("A span's ink owns only the rings it covers whole", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    // Two members of one strum with UNEQUAL rings: two beats and four. The span ends at the
    // shorter, which is the continuity law's box case.
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{2}),
        note(at(1, 1), 2, Fraction{4}, 7),
    };
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{2}},
    };

    const std::vector<bool> suppressed =
        chartSuppressedTails(presentedChartNotes(saved, map), shapes, {true}, map);

    REQUIRE(suppressed.size() == saved.size());
    // The shorter ring ends exactly at the span's end, so the mark owns it whole and it draws
    // nothing. This is the compression the rule exists for, and it is untouched.
    CHECK(suppressed[0]);
    // THE DISCRIMINATOR: the longer ring outlives the span, so it draws WHOLE, from its own head,
    // through the bracket and out. The retired rule hid its first two beats and drew the last two
    // starting at the bracket's edge — a ribbon with no head in front of it, which is the sighting
    // that killed the ternary.
    CHECK_FALSE(suppressed[1]);
    // And the presented ring itself is untouched — suppression is ink ownership and nothing else.
    const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
    CHECK(presented[0].sustain == Fraction{2});
    CHECK(presented[1].sustain == Fraction{4});
}

// The exemptions, each one the law's own rather than a special case bolted on: a marked tail is the
// canvas its marks live on, the other hand is a member of nothing, an uncovered ring has no mark
// standing over it, a silent hold has no tail to own, and a member presentation left tail-less has
// no ink to hide.
TEST_CASE("Suppression exempts marked tails, the other hand, and uncovered rings", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{4}},
    };
    // An ARPEGGIO span, for the reason the case above states: the box owns no ink to exempt.
    const auto suppressedFor = [&map, &shapes](const std::vector<ChartNote>& saved) {
        return chartSuppressedTails(presentedChartNotes(saved, map), shapes, {true}, map);
    };

    SECTION("a technique-bearing tail is the canvas its mark lives on")
    {
        // The shaken member keeps its whole ribbon; its plain partner is suppressed as usual, so
        // this cannot pass by nothing being suppressed anywhere.
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 1), 2, Fraction{2}, 7),
        };
        saved[1].vibrato = VibratoState::Narrow;

        const std::vector<bool> suppressed = suppressedFor(saved);

        REQUIRE(suppressed.size() == 2);
        CHECK(suppressed[0]);
        CHECK_FALSE(suppressed[1]);
    }

    SECTION("a right-hand onset is a member of nothing")
    {
        // A tap sounding over a held shape says nothing about the fretting hand, so the shape's
        // furniture owns none of its ring.
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 1), 2, Fraction{2}, 7),
            note(at(1, 3), 3, Fraction{2}, 9),
        };
        saved[2].attack = NoteAttack::Tap;

        const std::vector<bool> suppressed = suppressedFor(saved);

        REQUIRE(suppressed.size() == 3);
        // The strum's own members are covered whole, so this cannot pass by nothing being
        // suppressed anywhere.
        CHECK(suppressed[0]);
        CHECK(suppressed[1]);
        CHECK_FALSE(suppressed[2]);
    }

    SECTION("a ring no span covers keeps its whole tail")
    {
        // The second strum starts past the span's end, so nothing states its ring but itself.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            note(at(1, 1), 2, Fraction{2}, 7),
            note(at(2, 2), 1, Fraction{2}),
            note(at(2, 2), 2, Fraction{2}, 7),
        };

        const std::vector<bool> suppressed = suppressedFor(saved);

        REQUIRE(suppressed.size() == 4);
        CHECK(suppressed[0]);
        CHECK_FALSE(suppressed[2]);
        CHECK_FALSE(suppressed[3]);
    }

    SECTION("a silently-held stop has no tail to own")
    {
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}),
            heldStop(at(1, 1), 2, 7),
        };

        const std::vector<bool> suppressed = suppressedFor(saved);

        REQUIRE(suppressed.size() == 2);
        CHECK_FALSE(suppressed[1]);
    }

    SECTION("a member presentation left tail-less has nothing to suppress")
    {
        // A sub-quarter chug: rule 3 empties the group's tails, so there is no ribbon for the
        // bracket to have owned and nothing for a reader asking "is this note hiding ink?" to be
        // told yes about. The span still covers both members, which is what makes this a case.
        const std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 4}),
            note(at(1, 1), 2, Fraction{1, 4}, 7),
        };

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        const std::vector<bool> suppressed = suppressedFor(saved);

        REQUIRE(presented.size() == 2);
        CHECK(presented[0].sustain == Fraction{});
        REQUIRE(suppressed.size() == 2);
        CHECK_FALSE(suppressed[0]);
        CHECK_FALSE(suppressed[1]);
    }
}

// THE BRACKET ABSORBS; THE BOX DOES NOT (user ruling 2026-08-29). Ownership belongs to furniture
// that stands where the ribbons would be, and only an arpeggio's bracket does — it is drawn across
// the stretch its members arrive over. A chord box is drawn at an INSTANT and states a strum, so it
// never stood in for a ring, and its members' tails are simply their own.
TEST_CASE("A chord box owns none of its members' ink", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("a box span's ringing members draw their whole tails")
    {
        // One strum, two unequal rings, nothing carried or claimed and no tap: the shape sounds
        // whole, so the span arrives as a BOX and owns nothing.
        const SuppressedFigure figure = suppressedFigure(
            {
                note(at(1, 1), 1, Fraction{2}),
                note(at(1, 1), 2, Fraction{4}, 7),
            },
            map);

        REQUIRE(figure.shapes.size() == 1);
        REQUIRE(figure.arrivals.size() == 1);
        CHECK_FALSE(figure.arrivals[0]);
        REQUIRE(figure.suppressed.size() == 2);
        CHECK_FALSE(figure.suppressed[0]);
        CHECK_FALSE(figure.suppressed[1]);
        // And what draws is the ordinary presented tier and nothing else: both rings reach the
        // kept-sustain bound, so both members earn the tails they now keep.
        REQUIRE(figure.presented.size() == 2);
        CHECK(figure.presented[0].sustain == Fraction{2});
        CHECK(figure.presented[1].sustain == Fraction{4});
    }

    SECTION("a chug under a box still shows nothing, because presentation emptied it")
    {
        // The discrimination: what keeps a chugged riff clean under a box is rule 3's earning, not
        // suppression. No member reaches the kept-sustain bound and none carries a technique, so
        // the group presents no tail at all — there is nothing for the box to have owned.
        const SuppressedFigure figure = suppressedFigure(
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
        REQUIRE(figure.suppressed.size() == 2);
        CHECK_FALSE(figure.suppressed[0]);
        CHECK_FALSE(figure.suppressed[1]);
    }

    SECTION("a carried ring flips the class, and the bracket then owns the strum's ink")
    {
        // The let-ring figure, and the control for the section above: a ring crossing the strum's
        // onset joins the posture and makes the span an ARPEGGIO, so the same two struck members
        // that would have kept their tails under a box hand them to the bracket.
        const SuppressedFigure figure = suppressedFigure(
            {
                note(at(1, 1), 2, Fraction{4}, 7),
                note(at(1, 3), 1, Fraction{2}),
                note(at(1, 3), 3, Fraction{2}, 9),
            },
            map);

        REQUIRE(figure.shapes.size() == 1);
        REQUIRE(figure.arrivals.size() == 1);
        CHECK(figure.arrivals[0]);
        REQUIRE(figure.suppressed.size() == 3);
        // ALL THREE now, where the carried member used to keep its ribbon: THE DATING RULE (user
        // ruling 2026-08-31) puts the span's FRONT at that member's own onset, so the bracket
        // covers its whole ring rather than starting half way along it, and C3's all-or-nothing
        // half suppresses what the mark owns whole. The finding this section pins — a carried ring
        // flips the class and the bracket then owns the strum's ink — is unchanged and stronger:
        // the ink it owns is now the figure entire.
        CHECK(figure.suppressed[0]);
        CHECK(figure.suppressed[1]);
        CHECK(figure.suppressed[2]);
    }
}

// A span COVERING A GLIDE owns no ink either ([D2] amendment 1). It states the departing grip while
// the ribbons under it are travelling to another, so the mark and the ribbons stop saying the same
// thing and the warrant for hiding them lapses. The figure is the user's: fretted members slide
// while open strings ring and are picked again underneath.
TEST_CASE("A span covering travel suppresses nothing", "[core][chart]")
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

    const SuppressedFigure sliding = suppressedFigure(figure_of({{Fraction{2}, 12}}), map);

    // Two spans that tile at the landing: the departing grip covering the glide, and the successor
    // the landed grip opens.
    REQUIRE(sliding.shapes.size() == 2);
    CHECK(sliding.shapes[0].covers_travel);
    CHECK(sliding.shapes[1].carry_opened);
    CHECK_FALSE(sliding.shapes[1].covers_travel);
    // The class is NOT what answers here: the re-picked open strings sound part of the shape, so
    // the covering span is an arpeggio and would own ink if it were standing still.
    REQUIRE(sliding.arrivals.size() == 2);
    CHECK(sliding.arrivals[0]);
    REQUIRE(sliding.suppressed.size() == 6);
    for (std::size_t index = 0; index < sliding.suppressed.size(); ++index)
    {
        CAPTURE(index);
        CHECK_FALSE(sliding.suppressed[index]);
    }
    // WHICH members the carve-out still decides, now that suppression is all-or-nothing: the open
    // strings' FIRST rings end inside the span, cut by the re-pick, so without it they would vanish
    // whole under a mark that has stopped saying what their ribbons say. Their re-picked rings run
    // past the landing and would draw whole either way — the tail that once materialised there with
    // nothing leading into it is now unrepresentable, which is a second guard and not this one.
    REQUIRE(sliding.presented.size() == 6);
    CHECK(sliding.presented[2].sustain == Fraction{3, 4});
    CHECK(sliding.presented[4].sustain == Fraction{3});

    // The control, one channel apart: the same figure with the hand STILL states a span that
    // covers no travel, the re-picks still make it an arpeggio, and the bracket owns its members'
    // ink as it always has. It runs to its first member DEATH and hands the survivors on
    // (THE ACCUMULATION LAW, 2026-08-31), which is the second shape; what this control pins is the
    // first one's carve-out being absent, and that is asserted below.
    const SuppressedFigure still = suppressedFigure(figure_of({}), map);

    REQUIRE(still.shapes.size() >= 1);
    CHECK_FALSE(still.shapes[0].covers_travel);
    REQUIRE(still.arrivals.size() >= 1);
    CHECK(still.arrivals[0]);
    REQUIRE(still.suppressed.size() == 6);
    CHECK(still.suppressed[0]);
    CHECK(still.suppressed[2]);
    CHECK(still.suppressed[4]);
}

// THE N5 FIGURE, which the two-record seam made undrawable: a lone ringing note FOLDS INTO a
// chord's posture and then glides under it. The fold-in's travel used to live in the record the
// covers_travel test could not read, so the span went on owning its members' ink across a transit
// it did not know was happening — and the chord's static neighbours vanished under a mark that was
// no longer saying what their ribbons would say. With one per-string record the carve-out fires.
TEST_CASE("A fold-in's own glide stops the span suppressing anything", "[core][chart]")
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

    const SuppressedFigure gliding =
        suppressedFigure(figure_of({{Fraction{2}, 9}, {Fraction{3}, 12}}), map);

    // The span DATES from the ringing note (THE ACCUMULATION LAW, 2026-08-31), so that ring is a
    // founding member and bounds the statement at its own landing; the chord's members ring past
    // it and hold the seamless successor. What this case pins is the first span's carve-out.
    REQUIRE(gliding.shapes.size() >= 1);
    CHECK(gliding.shapes[0].covers_travel);
    // The class is not what answers: the carry makes this an arpeggio, so the span would own its
    // members' ink if it were standing still.
    REQUIRE(gliding.arrivals.size() >= 1);
    CHECK(gliding.arrivals[0]);
    // Nobody's ink is suppressed — the STATIC neighbours' tails draw straight through the transit,
    // which is the half of the figure the reader actually notices.
    REQUIRE(gliding.suppressed.size() == 3);
    for (std::size_t index = 0; index < gliding.suppressed.size(); ++index)
    {
        CAPTURE(index);
        CHECK_FALSE(gliding.suppressed[index]);
    }

    // The control, one keyframe apart: the same carry HOLDING its stop states no travel, so the
    // bracket owns its members' ink exactly as it always has.
    const SuppressedFigure planted = suppressedFigure(figure_of({{Fraction{2}, 9}}), map);

    REQUIRE(planted.shapes.size() == 1);
    CHECK_FALSE(planted.shapes[0].covers_travel);
    REQUIRE(planted.arrivals.size() == 1);
    CHECK(planted.arrivals[0]);
    REQUIRE(planted.suppressed.size() == 3);
    CHECK(planted.suppressed[1]);
    CHECK(planted.suppressed[2]);
}

} // namespace rock_hero::common::core
