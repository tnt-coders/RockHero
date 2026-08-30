#include "highway/highway_atlas.h"
#include "highway/highway_head_marks.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

[[nodiscard]] common::core::NoteViewState noteWith(
    const common::core::NoteAttack attack, const common::core::LegatoMotion motion)
{
    common::core::NoteViewState note;
    note.string = 1;
    note.fret = 5;
    note.attack = attack;
    note.legato = motion;
    return note;
}

[[nodiscard]] std::vector<int> cellsOf(const HighwayHeadMarkStack& stack)
{
    std::vector<int> cells;
    for (const HighwayHeadMark& mark : stack)
    {
        cells.push_back(mark.cell);
    }
    return cells;
}

} // namespace

// The 3D head's connection mark comes from the RESOLVED motion, and the two directions are one
// atlas cell drawn two ways. The mapping had no witness while it lived inline in the draw pass,
// where a mutation reading the stored attack instead — drawing a hammer for every `Legato` —
// changed nothing any test could see.
TEST_CASE("Highway legato cell follows the resolved motion", "[ui][highway]")
{
    CHECK(highwayLegatoCell(common::core::LegatoMotion::Hammer) == HighwayLegatoCell::Upright);
    CHECK(highwayLegatoCell(common::core::LegatoMotion::Pull) == HighwayLegatoCell::Flipped);
    // The whole no-indicator ruling: a claim nothing justifies draws what the plain pick draws.
    CHECK(highwayLegatoCell(common::core::LegatoMotion::Unjustified) == HighwayLegatoCell::None);

    // The stored attack cannot reach the answer. A `Legato` whose claim broke draws nothing, and a
    // plain `Pick` the resolver never asked about draws nothing either.
    CHECK(
        highwayLegatoCell(
            noteWith(common::core::NoteAttack::Legato, common::core::LegatoMotion::Unjustified)
                .legato) == HighwayLegatoCell::None);
    // A left-hand tap resolves to the hammer motion unconditionally, so it wears the upright cell
    // — including on an open string with a node, which the open-head path used to drop.
    CHECK(
        highwayLegatoCell(
            noteWith(common::core::NoteAttack::LeftTap, common::core::LegatoMotion::Hammer)
                .legato) == HighwayLegatoCell::Upright);
}

// The darker technique base: it must follow the mark that is actually DRAWN, so a broken claim
// leaves the head standard while a resolved one darkens it.
TEST_CASE("Highway tech head follows the drawn marks", "[ui][highway]")
{
    CHECK_FALSE(highwayTechHead(
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified)));
    CHECK_FALSE(highwayTechHead(
        noteWith(common::core::NoteAttack::Legato, common::core::LegatoMotion::Unjustified)));
    CHECK(highwayTechHead(
        noteWith(common::core::NoteAttack::Legato, common::core::LegatoMotion::Hammer)));
    CHECK(highwayTechHead(
        noteWith(common::core::NoteAttack::Legato, common::core::LegatoMotion::Pull)));

    // The other two clauses, so the connection one cannot be masking them: a dead note and a
    // scrape's unpitched travel.
    common::core::NoteViewState dead =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    dead.dead = true;
    CHECK(highwayTechHead(dead));

    // Keyed on the dead flag ALONE. A palm mute is still a pitched note, so it leaves the head
    // standard; a note carrying both sounds dead, so it takes the dead base and its palm marker
    // stacks over that.
    common::core::NoteViewState palm =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    palm.palm_mute = true;
    CHECK_FALSE(highwayTechHead(palm));

    common::core::NoteViewState both = dead;
    both.palm_mute = true;
    CHECK(highwayTechHead(both));

    CHECK(highwayTechHead(
        noteWith(common::core::NoteAttack::PickSlide, common::core::LegatoMotion::Unjustified)));

    // A node head is deliberately NOT a tech head anymore: it wears its own round base, which
    // outranks the darkening.
    common::core::NoteViewState artificial_harmonic =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    artificial_harmonic.harmonic_node = 17.0;
    CHECK_FALSE(highwayTechHead(artificial_harmonic));
}

// The round node base follows the board's own placement rule: exactly the heads DRAWN on a node
// take it, so the base shape and the head station can never disagree.
TEST_CASE("Highway node head follows the drawn sounding position", "[ui][highway]")
{
    // No node, no round base.
    CHECK_FALSE(highwayNodeHead(
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified)));

    // A picked harmonic's head sits on its node.
    common::core::NoteViewState artificial_harmonic =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    artificial_harmonic.harmonic_node = 17.0;
    CHECK(highwayNodeHead(artificial_harmonic));

    // A tap harmonic strikes the node directly, so its head sits there too — the placement rule
    // ignores which hand owns the node, unlike the fretting-hand predicates.
    common::core::NoteViewState tap_harmonic =
        noteWith(common::core::NoteAttack::Tap, common::core::LegatoMotion::Unjustified);
    tap_harmonic.harmonic_node = 12.0;
    CHECK(highwayNodeHead(tap_harmonic));

    // A pinch's node is the picking hand's graze over the body; its head stays on the stop and
    // keeps the family rectangle.
    common::core::NoteViewState pinch =
        noteWith(common::core::NoteAttack::Pinch, common::core::LegatoMotion::Unjustified);
    pinch.harmonic_node = 17.0;
    CHECK_FALSE(highwayNodeHead(pinch));

    // A node past the drawn board still draws AS a node (capped to the last fret), so it keeps
    // the round base.
    common::core::NoteViewState far_node =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    far_node.harmonic_node = 40.0;
    CHECK(highwayNodeHead(far_node));
}

// The ORDER is the whole reason this authority exists. It used to be hand-written in the
// open-string branch and again in the fretted one, and the two had already drifted: the open
// branch drew the connection cell underneath everything and no harmonic at all, while the fretted
// branch drew the harmonic at the very bottom and the connection cell fifth. Neither draw branch
// is reachable from a test, so the divergence had no witness until the list moved here.
TEST_CASE("Highway head marks stack in override order", "[ui][highway]")
{
    // Deliberately synthetic: every rung at once, which is also the provable maximum a head can
    // wear. No chart need produce this note — the case exists to pin the ladder, and pinning it
    // needs all five rungs present at the same time.
    common::core::NoteViewState loaded =
        noteWith(common::core::NoteAttack::Tap, common::core::LegatoMotion::Hammer);
    loaded.palm_mute = true;
    loaded.harmonic_node = 12.0;
    loaded.dead = true;

    const HighwayHeadMarkStack stack = highwayHeadMarks(loaded);
    REQUIRE(stack.count == HighwayHeadMarkStack::g_capacity);
    CHECK(
        cellsOf(stack) == std::vector<int>{
                              g_head_cell_palm_mute,
                              g_head_cell_tap,
                              g_head_cell_legato,
                              g_head_cell_harmonic,
                              g_head_cell_full_mute
                          });

    // A head wearing nothing stacks nothing, so a plain pick costs no markers at all.
    CHECK(
        highwayHeadMarks(
            noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified))
            .count == 0);
}

// The deadening X is the mark that must survive intact — a broken X reads as a different mark —
// so it draws over everything. Both marks below used to cut it: the connection cell on a fretted
// head, and slap on an open string.
TEST_CASE("Highway dead X draws over every other head mark", "[ui][highway]")
{
    common::core::NoteViewState legato_dead =
        noteWith(common::core::NoteAttack::Legato, common::core::LegatoMotion::Pull);
    legato_dead.dead = true;
    CHECK(
        cellsOf(highwayHeadMarks(legato_dead)) ==
        std::vector<int>{g_head_cell_legato, g_head_cell_full_mute});

    common::core::NoteViewState slap_dead =
        noteWith(common::core::NoteAttack::Slap, common::core::LegatoMotion::Unjustified);
    slap_dead.dead = true;
    CHECK(
        cellsOf(highwayHeadMarks(slap_dead)) ==
        std::vector<int>{g_head_cell_slap, g_head_cell_full_mute});
}

// A scrape is a category of one, and that is a chart rule rather than a layering choice: the
// validator holds a pick-slide note to savedChartNote(note) == note, a fixpoint that strips every
// mute, node, vibrato, tremolo and bend. The stack must therefore refuse to stack anything on it
// even when handed a view that carries the flags anyway.
TEST_CASE("Highway scrape wears its pick mark alone", "[ui][highway]")
{
    common::core::NoteViewState scrape =
        noteWith(common::core::NoteAttack::PickSlide, common::core::LegatoMotion::Hammer);
    scrape.palm_mute = true;
    scrape.dead = true;
    scrape.harmonic_node = 12.0;
    CHECK(cellsOf(highwayHeadMarks(scrape)) == std::vector<int>{g_head_cell_pick_slide});
}

// The two harmonic cells are one rung, not two: a pinch and a node are the same claim about pitch
// made by different hands, and the attack slot can only hold one of them.
TEST_CASE("Highway harmonic rung takes the pinch cell or the node cell", "[ui][highway]")
{
    common::core::NoteViewState pinch =
        noteWith(common::core::NoteAttack::Pinch, common::core::LegatoMotion::Unjustified);
    pinch.harmonic_node = 17.0;
    CHECK(cellsOf(highwayHeadMarks(pinch)) == std::vector<int>{g_head_cell_pinch_harmonic});

    common::core::NoteViewState natural =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    natural.harmonic_node = 12.0;
    CHECK(cellsOf(highwayHeadMarks(natural)) == std::vector<int>{g_head_cell_harmonic});
}

// The head's harmonic cell and the note's node-centred fret-span line on the floor read ONE
// predicate, so this pins the classification both of them stand on rather than either drawer's
// own reading of it.
TEST_CASE("Highway harmonic mark covers the nodes the board can point at", "[ui][highway]")
{
    common::core::NoteViewState note =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    CHECK_FALSE(highwayHarmonicMark(note)); // no node at all

    // A natural (the stop is the node itself), an artificial over a real stop, and a tapped one:
    // every node the fretting hand or the tapping hand puts ON the neck.
    note.harmonic_node = 12.0;
    note.fret = 0;
    CHECK(highwayHarmonicMark(note));
    note.fret = 5;
    note.harmonic_node = 17.0;
    CHECK(highwayHarmonicMark(note));
    note.attack = common::core::NoteAttack::Tap;
    CHECK(highwayHarmonicMark(note));

    // A pinch's node is over the body, so the neck has nowhere to point and the head takes its
    // own cell instead.
    note.attack = common::core::NoteAttack::Pinch;
    CHECK_FALSE(highwayHarmonicMark(note));

    // A scrape's node is the in-memory latent its attack toggle preserves; no finger touches it.
    note.attack = common::core::NoteAttack::PickSlide;
    CHECK_FALSE(highwayHarmonicMark(note));
}

// Rotation travels WITH each mark because the ladder interleaves the two kinds: the harmonic rides
// the head's rolling flip and the connection cell directly beneath it does not. A call site that
// re-derived this from the mark's position in the list would get it wrong.
TEST_CASE("Highway head marks carry their own roll behavior", "[ui][highway]")
{
    common::core::NoteViewState note =
        noteWith(common::core::NoteAttack::Tap, common::core::LegatoMotion::Pull);
    note.palm_mute = true;
    note.harmonic_node = 12.0;
    note.dead = true;

    const HighwayHeadMarkStack stack = highwayHeadMarks(note);
    REQUIRE(stack.count == HighwayHeadMarkStack::g_capacity);
    CHECK(stack.marks.at(0).rides_roll);       // palm
    CHECK(stack.marks.at(1).rides_roll);       // tap
    CHECK_FALSE(stack.marks.at(2).rides_roll); // connection
    CHECK(stack.marks.at(3).rides_roll);       // harmonic
    CHECK_FALSE(stack.marks.at(4).rides_roll); // dead X

    // The pull-off is the connection cell mirrored, and only that mark is ever mirrored.
    CHECK(stack.marks.at(2).flipped);
    CHECK_FALSE(stack.marks.at(0).flipped);
    CHECK_FALSE(stack.marks.at(3).flipped);

    const common::core::NoteViewState hammer =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Hammer);
    REQUIRE(highwayHeadMarks(hammer).count == 1);
    CHECK_FALSE(highwayHeadMarks(hammer).marks.at(0).flipped);
}

} // namespace rock_hero::common::ui
