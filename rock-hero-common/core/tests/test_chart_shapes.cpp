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

// The derivation as every reader gets it: from the saved stream and the picture it presents.
[[nodiscard]] ChartShapes deriveFrom(const std::vector<ChartNote>& notes)
{
    const TempoMap tempo_map = makeTempoMap();
    return deriveChartShapes(notes, presentedChartNotes(notes, tempo_map), tempo_map);
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
