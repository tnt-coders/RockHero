#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shape_holds.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Sixteen seconds of default 4/4 — eight bars — so a whole note is one bar from `0` at 1:1.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// A picked note at a measure and beat, ringing a quarter unless a case says otherwise.
[[nodiscard]] ChartNote noteAt(
    const int measure, const int beat, const int string, const int fret,
    const Fraction ring = Fraction{1, 4})
{
    ChartNote note;
    note.position = GridPosition{.measure = measure, .beat = beat};
    note.string = string;
    note.fret = fret;
    note.sustain = ring;
    return note;
}

// A two-hand tap landing at `fret`.
[[nodiscard]] ChartNote tapAt(const int measure, const int beat, const int string, const int fret)
{
    ChartNote note = noteAt(measure, beat, string, fret);
    note.attack = NoteAttack::Tap;
    return note;
}

// A note whose fret channel states `fret` at `offset` along its ring.
[[nodiscard]] ChartNote withFretAt(ChartNote note, const Fraction offset, const int fret)
{
    note.keyframes.push_back(Keyframe{.offset = offset, .fret = fret});
    return note;
}

// An unnamed hold from one downbeat-relative beat to another.
[[nodiscard]] ShapeHold holdOver(
    const int start_measure, const int start_beat, const int end_measure, const int end_beat)
{
    return ShapeHold{
        .start = GridPosition{.measure = start_measure, .beat = start_beat},
        .end = GridPosition{.measure = end_measure, .beat = end_beat},
        .grip = UnnamedGrip{},
    };
}

// The end shape rule 9 gives `hold` over `notes`, on the whole-note axis.
[[nodiscard]] Fraction normalEndOf(const std::vector<ChartNote>& notes, const ShapeHold& hold)
{
    const TempoMap tempo_map = makeTempoMap();
    return shapeHoldNormalEnd(chartConnections(notes, tempo_map), tempo_map, hold);
}

// Whether `hold` fits `notes` by shape rule 9.
[[nodiscard]] bool fitsNotes(const std::vector<ChartNote>& notes, const ShapeHold& hold)
{
    const TempoMap tempo_map = makeTempoMap();
    return shapeHoldFitsNotes(chartConnections(notes, tempo_map), tempo_map, hold);
}

} // namespace

// Shape rule 9's conflict: a later note on a stated string releasing its stop under hand-position
// rule 6, at the release instant. The hold is the first two bars, `[0, 2)` in whole notes, and its
// notes end before its end, so only a conflict moves the answer.
TEST_CASE("A shape hold conflicts where a stated stop is released", "[core][chart]")
{
    const ShapeHold hold = holdOver(1, 1, 3, 1);
    const ChartNote stated = noteAt(1, 1, 1, 5);

    SECTION("a higher fret on the string is another finger and fits")
    {
        CHECK(fitsNotes({stated, noteAt(1, 3, 1, 7)}, hold));
    }

    SECTION("a lower fret conflicts at its onset")
    {
        CHECK(normalEndOf({stated, noteAt(1, 3, 1, 3)}, hold) == Fraction{1, 2});
    }

    SECTION("the open string conflicts at its onset")
    {
        CHECK(normalEndOf({stated, noteAt(1, 3, 1, 0)}, hold) == Fraction{1, 2});
    }

    SECTION("a natural harmonic's node conflicts at its onset")
    {
        ChartNote chime = noteAt(1, 3, 1, 0);
        chime.harmonic_node = 12.0;
        CHECK(normalEndOf({stated, chime}, hold) == Fraction{1, 2});
    }

    SECTION("a tap at or below the stop conflicts, and one above fits")
    {
        CHECK(normalEndOf({stated, tapAt(1, 3, 1, 5)}, hold) == Fraction{1, 2});
        CHECK(normalEndOf({stated, tapAt(1, 3, 1, 3)}, hold) == Fraction{1, 2});
        CHECK(fitsNotes({stated, tapAt(1, 3, 1, 7)}, hold));
    }

    SECTION("the stating finger sliding away conflicts where it lands")
    {
        const ChartNote sliding = withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 4}, 7);
        CHECK(normalEndOf({sliding}, hold) == Fraction{1, 4});
    }

    SECTION("another note sliding onto the stated fret conflicts where it lands")
    {
        const ChartNote arriving =
            withFretAt(noteAt(1, 2, 1, 7, Fraction{1, 2}), Fraction{1, 4}, 5);
        CHECK(normalEndOf({stated, arriving}, hold) == Fraction{1, 2});
    }

    SECTION("a rest and a restrike of the stop never conflict")
    {
        CHECK(fitsNotes({stated, noteAt(2, 1, 1, 5)}, hold));
    }

    SECTION("another string never conflicts")
    {
        CHECK(fitsNotes({stated, noteAt(1, 2, 2, 0), noteAt(1, 3, 2, 2)}, hold));
    }

    SECTION("the earliest conflict across the strings is the answer")
    {
        CHECK(
            normalEndOf(
                {stated, noteAt(1, 1, 2, 7), noteAt(1, 3, 2, 5), noteAt(1, 4, 1, 3)}, hold) ==
            Fraction{1, 2});
    }
}

// Only notes struck inside a hold state its stops: a ring sounding in from before the start states
// nothing, so a lower fret after it is the string's first statement rather than a conflict.
TEST_CASE("A ring sounding into a shape hold states nothing", "[core][chart]")
{
    const ShapeHold hold = holdOver(1, 3, 3, 1);
    CHECK(fitsNotes({noteAt(1, 1, 1, 5, Fraction{1}), noteAt(2, 1, 1, 3)}, hold));
}

// A hold stating no pressed stop always passes, whatever rings across its end, and a none hold
// states no stop at all.
TEST_CASE("A shape hold stating no pressed stop fits its notes", "[core][chart]")
{
    const ShapeHold hold = holdOver(1, 1, 2, 1);

    SECTION("open strings, a natural harmonic and a tap state nothing")
    {
        ChartNote chime = noteAt(1, 2, 2, 0, Fraction{2});
        chime.harmonic_node = 12.0;
        CHECK(fitsNotes({noteAt(1, 1, 1, 0, Fraction{2}), chime, tapAt(1, 3, 3, 9)}, hold));
    }

    SECTION("a none hold keeps its end over a conflict and a ring across it")
    {
        ShapeHold none = hold;
        none.grip = NoneGrip{};
        CHECK(fitsNotes({noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 3, Fraction{2})}, none));
    }
}

// Shape rule 9's end check: the end fails where every stated stop still sounds across it, from a
// note struck inside that grips it. The hold is the first bar, `[0, 1)` in whole notes.
TEST_CASE("A shape hold's end passes once a stated stop falls silent", "[core][chart]")
{
    const ShapeHold hold = holdOver(1, 1, 2, 1);

    SECTION("a stop ringing across the end alone extends the hold to its ring's end")
    {
        CHECK(normalEndOf({noteAt(1, 1, 1, 5, Fraction{3, 2})}, hold) == Fraction{3, 2});
    }

    SECTION("a ring ending exactly at the end has stopped sounding")
    {
        CHECK(fitsNotes({noteAt(1, 1, 1, 5, Fraction{1})}, hold));
    }

    SECTION("one stated stop silent before the end passes, though another rings across")
    {
        CHECK(fitsNotes({noteAt(1, 1, 1, 5, Fraction{3, 2}), noteAt(1, 1, 2, 7)}, hold));
    }

    SECTION("every stated stop across the end extends to the first that falls silent")
    {
        CHECK(
            normalEndOf(
                {noteAt(1, 1, 1, 5, Fraction{3, 2}), noteAt(1, 1, 2, 7, Fraction{5, 4})}, hold) ==
            Fraction{5, 4});
    }

    SECTION("a higher fret ringing across the end has ended the stated stop's sound")
    {
        CHECK(
            fitsNotes({noteAt(1, 1, 1, 5, Fraction{1, 2}), noteAt(1, 3, 1, 7, Fraction{1})}, hold));
    }

    SECTION("a restrike of the stated stop inside the hold still sounds it")
    {
        CHECK(
            normalEndOf(
                {noteAt(1, 1, 1, 5, Fraction{1, 2}), noteAt(1, 3, 1, 5, Fraction{1})}, hold) ==
            Fraction{3, 2});
    }

    SECTION("a slide away past the end silences the stop where it lands")
    {
        const ChartNote sliding = withFretAt(noteAt(1, 1, 1, 5, Fraction{2}), Fraction{3, 2}, 7);
        CHECK(normalEndOf({sliding}, hold) == Fraction{3, 2});
    }

    SECTION("a conflict inside crops the hold even where its end would extend")
    {
        CHECK(
            normalEndOf(
                {noteAt(1, 1, 1, 5, Fraction{2}), noteAt(1, 1, 2, 7), noteAt(1, 3, 2, 3)}, hold) ==
            Fraction{1, 2});
    }
}

// The earliest fitting end comes from one sweep past the end: a string first struck inside the
// extension states its stop there, and ends the extension sooner where that stop falls silent
// first. An open string so struck states nothing.
TEST_CASE("A shape hold's extension counts a string first struck inside it", "[core][chart]")
{
    const ShapeHold hold = holdOver(1, 1, 2, 1);
    const ChartNote ringing = noteAt(1, 1, 1, 5, Fraction{2});

    SECTION("a stop struck inside the extension that falls silent first ends it")
    {
        CHECK(normalEndOf({ringing, noteAt(2, 2, 2, 7)}, hold) == Fraction{3, 2});
    }

    SECTION("an open string struck inside the extension states nothing")
    {
        CHECK(normalEndOf({ringing, noteAt(2, 2, 2, 0)}, hold) == Fraction{2});
    }

    SECTION("a stop falling silent after the stated stops does not move the end")
    {
        CHECK(normalEndOf({ringing, noteAt(2, 4, 2, 7, Fraction{1})}, hold) == Fraction{2});
    }

    SECTION("a glide away on a string first struck inside the extension ends it where it lands")
    {
        const ChartNote gliding = withFretAt(noteAt(2, 2, 2, 7, Fraction{1}), Fraction{1, 4}, 9);
        CHECK(normalEndOf({ringing, gliding}, hold) == Fraction{3, 2});
    }
}

// Nothing sounds past the song's end, so a ring crossing it extends no hold: a hold ending at the
// song's end fits its notes there. The timeline's last bar is the eighth, ending at 8.
TEST_CASE("A shape hold never extends past the song's end", "[core][chart]")
{
    const ShapeHold last_bar = holdOver(8, 1, 9, 1);
    CHECK(fitsNotes({noteAt(8, 1, 1, 5, Fraction{2})}, last_bar));
    CHECK(normalEndOf({noteAt(8, 1, 1, 5, Fraction{2})}, holdOver(8, 1, 8, 3)) == Fraction{8});
}

} // namespace rock_hero::common::core
