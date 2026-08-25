#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
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
        .bend = {},
        .slides = {},
    };
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

// Rule 1's one exemption: a ring running strictly PAST the onset that binds it is a deliberate
// hold — a tie merged across a neighbour, a cross-voice hold — and presents whole however many
// later onsets it crosses. Its neighbours still trim normally, which is what distinguishes the
// exemption from simply switching the rule off.
TEST_CASE("Rule 1 presents a ring past its binding onset in full", "[core][chart]")
{
    const TempoMap map = fourFourMap();
    const std::vector<ChartNote> saved = {
        note(at(1, 1), 1, Fraction{4}),
        note(at(1, 2), 2, Fraction{1}),
        note(at(1, 3), 2, Fraction{1}),
        note(at(2, 1), 2, Fraction{1}),
    };

    const std::vector<Fraction> presented = presentedSustains(saved, map);
    REQUIRE(presented.size() == saved.size());
    // The whole-bar ring crosses three later onsets and keeps every beat of them.
    CHECK(presented[0] == Fraction{4});
    // Its neighbour a beat later is bound by the onset after it and trims to the margin.
    CHECK(presented[1] == Fraction{3, 4});
    // Two beats of clearance leaves this one-beat ring alone.
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
        saved[0].bend = {
            BendPoint{.offset = Fraction{1, 2}, .semitones = 0.5},
            BendPoint{.offset = Fraction{15, 8}, .semitones = 1.0},
        };

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The margin alone would have stopped at 7/4; the second bend point still says something
        // new at 15/8, so the tail runs to it and stops exactly there.
        CHECK(presented[0].sustain == Fraction{15, 8});
        CHECK(presented[0].bend.size() == 2);
    }

    SECTION("trailing points that repeat the curve leave with the tail")
    {
        saved[0].bend = {
            BendPoint{.offset = Fraction{1, 2}, .semitones = 1.0},
            BendPoint{.offset = Fraction{15, 8}, .semitones = 1.0},
            BendPoint{.offset = Fraction{2}, .semitones = 1.0},
        };

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
        REQUIRE(presented[0].bend.size() == 1);
        // Clipped, never rescaled: the surviving point keeps the offset the source authored.
        CHECK(presented[0].bend.front().offset == Fraction{1, 2});
        // The saved curve is untouched, so a later reveal can still draw the whole ring.
        CHECK(saved[0].bend.size() == 3);
    }

    SECTION("an equal-fret hold waypoint is a pin, not a change")
    {
        saved[0].slides = {
            SlideWaypoint{.offset = Fraction{1, 2}, .fret = 7},
            SlideWaypoint{.offset = Fraction{15, 8}, .fret = 7},
        };

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{7, 4});
        REQUIRE(presented[0].slides.size() == 1);
        CHECK(presented[0].slides.front().offset == Fraction{1, 2});
    }

    SECTION("a waypoint gliding to a new fret floors the trim like a bend does")
    {
        saved[0].slides = {
            SlideWaypoint{.offset = Fraction{1, 2}, .fret = 7},
            SlideWaypoint{.offset = Fraction{15, 8}, .fret = 9},
        };

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0].sustain == Fraction{15, 8});
        CHECK(presented[0].slides.size() == 2);
    }
}

// Rule 2's slide-out clause: an unpitched trail-off is gesture geometry rather than protected
// payload, so its presented terminal compresses back with the tail — but never below the minimum
// slide window, and never onto or before the last waypoint that survived the clip.
TEST_CASE("Rule 2 compresses a slide-out to the smallest legal end", "[core][chart]")
{
    const TempoMap map = fourFourMap();

    SECTION("the compressed end floors at the minimum slide window")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 4}),
            note(at(1, 1, Fraction{1, 4}), 2, Fraction{1}),
        };
        saved[0].slide_out = SlideOut{.offset = Fraction{1, 4}, .fret = 8};

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
            CHECK(first.slide_out->offset == g_minimum_slide_window);
        }
    }

    SECTION("the compressed end is bumped strictly past the last surviving waypoint")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1, 2}),
            note(at(1, 1, Fraction{1, 2}), 2, Fraction{1}),
        };
        saved[0].slides = {SlideWaypoint{.offset = Fraction{1, 4}, .fret = 7}};
        saved[0].slide_out = SlideOut{.offset = Fraction{1, 2}, .fret = 9};

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        REQUIRE(presented.size() == saved.size());
        // The margin line lands exactly on the surviving waypoint, so the trail-off takes the
        // first legal offset past it — one minimum window on — rather than sitting on it.
        // Bound once rather than indexed per assertion: each presented[0] is a separate
        // operator[] call, which the unchecked-optional-access analysis cannot tie back to
        // the guard, so the guard only reaches the access through a single name.
        const ChartNote& first = presented[0];
        CHECK(first.sustain == Fraction{3, 8});
        REQUIRE(first.slide_out.has_value());
        if (first.slide_out.has_value())
        {
            CHECK(first.slide_out->offset == Fraction{3, 8});
        }
        CHECK(first.slides.size() == 1);
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
        saved[0].slide_out = SlideOut{.offset = Fraction{2}, .fret = 3};

        const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
        REQUIRE(presented.size() == saved.size());
        // Bound once rather than indexed per assertion: each presented[0] is a separate
        // operator[] call, which the unchecked-optional-access analysis cannot tie back to
        // the guard, so the guard only reaches the access through a single name.
        const ChartNote& first = presented[0];
        REQUIRE(first.slide_out.has_value());
        // The single leg starts at the onset, so it has room to end on the margin line exactly and
        // gives up no spacing at all.
        CHECK(first.sustain == Fraction{7, 4});
        if (first.slide_out.has_value())
        {
            CHECK(first.slide_out->offset == first.sustain);
        }
    }

    SECTION("a leg starting inside the margin halves its distance to the onset")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{2}, 12),
            note(at(1, 3), 2, Fraction{1}),
        };
        saved[0].attack = NoteAttack::PickSlide;
        saved[0].slides = {SlideWaypoint{.offset = Fraction{15, 8}, .fret = 7}};
        saved[0].slide_out = SlideOut{.offset = Fraction{2}, .fret = 3};

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
        if (first.slide_out.has_value())
        {
            CHECK(first.slide_out->offset == first.sustain);
        }
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
    saved[1].vibrato = true;

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
    saved[0].bend = {
        BendPoint{.offset = Fraction{}, .semitones = 0.0},
        BendPoint{.offset = Fraction{1, 2}, .semitones = 2.0},
    };

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
    saved[2].slides = {SlideWaypoint{.offset = Fraction{1}, .fret = 7}};
    saved[3].slide_out = SlideOut{.offset = Fraction{2}, .fret = 8};

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
        saved[0].vibrato = true;

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        CHECK(presented[0] == Fraction{5, 8});
        // The ornament's own lead-length tail does not survive: its principal an eighth later binds
        // it, and the margin is wider than the gap.
        CHECK(presented[1] == Fraction{});
        CHECK(presented[2] == Fraction{1});
    }

    SECTION("the same geometry with a longer ring is a deliberate hold instead")
    {
        std::vector<ChartNote> saved = {
            note(at(1, 1), 1, Fraction{1}),
            note(at(1, 1, Fraction{7, 8}), 2, Fraction{1, 8}, 7),
            note(at(1, 2), 2, Fraction{1}, 8),
        };
        saved[0].vibrato = true;

        const std::vector<Fraction> presented = presentedSustains(saved, map);
        REQUIRE(presented.size() == saved.size());
        // A full beat runs strictly past the onset at 7/8, so rule 1 exempts it. Binding on the
        // SOUNDING position is what the model buys: the import policy read a separately notated
        // beat instead and trimmed this ring to 5/8.
        CHECK(presented[0] == Fraction{1});
    }
}

// The span-implied hold, now read from the stored ring instead of invented: a chug under a hand
// shape presents no tail at all, yet the shape is what tells the player to keep holding it.
TEST_CASE("A span holds a chug for its actual ring, capped by the span", "[core][chart]")
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
    // The span ends a quarter beat into the third strum, so the last group is the one the span cap
    // bites on while the first two are capped by their own rings.
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = at(1, 1), .sustain = Fraction{9, 4}},
    };

    const std::vector<ChartNote> presented = presentedChartNotes(saved, map);
    const std::vector<Fraction> holds = chartHolds(saved, presented, shapes, map);
    REQUIRE(holds.size() == saved.size());
    // Every chug is a half-beat effect-free ring, so rule 3 presents no tail on any of them.
    for (const ChartNote& shown : presented)
    {
        CHECK(shown.sustain == Fraction{});
    }
    // The first two strums hold for their actual rings: shorter than both the span's remainder and
    // the restrike a beat later.
    CHECK(holds[0] == Fraction{1, 2});
    CHECK(holds[1] == Fraction{1, 2});
    CHECK(holds[2] == Fraction{1, 2});
    CHECK(holds[3] == Fraction{1, 2});
    // The last strum's ring outlives the span, so the span's end is what the hand is held to.
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
        const std::vector<Fraction> holds = chartHolds(saved, presented, shapes, map);
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
        const std::vector<Fraction> holds = chartHolds(saved, presented, shapes, map);
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
        const std::vector<Fraction> holds = chartHolds(saved, presented, shapes, map);
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
        const std::vector<Fraction> holds = chartHolds(saved, presented, shapes, map);
        REQUIRE(holds.size() == 2);
        // A quarter beat of span A is left, which is shorter than the ring, so it is the cap. A
        // cursor that remembered only span B would find no cover at all and hold nothing.
        CHECK(holds[0] == Fraction{1, 4});
        CHECK(holds[1] == Fraction{1, 4});

        // Listing order must not matter either: the same two spans the other way round give the
        // same answer, which a last-writer-wins cursor could not promise.
        const std::vector<ChartShape> reversed = {shapes[1], shapes[0]};
        const std::vector<Fraction> held_reversed = chartHolds(saved, presented, reversed, map);
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
        const std::vector<Fraction> holds = chartHolds(saved, presented, shapes, map);
        REQUIRE(holds.size() == saved.size());
        // The second span reaches two beats past the strum, so each member holds its own ring.
        CHECK(holds[0] == Fraction{1, 2});
        CHECK(holds[1] == Fraction{1, 2});
        // No span covers measure 5, so its strum holds exactly what it presents: nothing.
        CHECK(holds[2] == Fraction{});
        CHECK(holds[3] == Fraction{});
    }
}

} // namespace rock_hero::common::core
