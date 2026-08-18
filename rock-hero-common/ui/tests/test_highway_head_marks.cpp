#include "highway/highway_head_marks.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::common::ui
{

namespace
{

[[nodiscard]] common::core::HighwayNoteView noteWith(
    const common::core::NoteAttack attack, const common::core::LegatoMotion motion)
{
    common::core::HighwayNoteView note;
    note.string = 1;
    note.fret = 5;
    note.attack = attack;
    note.legato = motion;
    return note;
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

    // The other clause, so the connection one cannot be masking it: a full mute.
    common::core::HighwayNoteView muted =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    muted.mute = common::core::NoteMute::Full;
    CHECK(highwayTechHead(muted));

    // A scrape is deliberately NOT a tech head: it draws no base at all (the bare-scrape
    // experiment), so a predicate claiming one for it would darken a head that never lands.
    CHECK_FALSE(highwayTechHead(
        noteWith(common::core::NoteAttack::PickSlide, common::core::LegatoMotion::Unjustified)));

    // A node head is deliberately NOT a tech head anymore: it wears its own round base, which
    // outranks the darkening.
    common::core::HighwayNoteView artificial_harmonic =
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
    common::core::HighwayNoteView artificial_harmonic =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    artificial_harmonic.harmonic_node = 17.0;
    CHECK(highwayNodeHead(artificial_harmonic));

    // A tap harmonic strikes the node directly, so its head sits there too — the placement rule
    // ignores which hand owns the node, unlike the fretting-hand predicates.
    common::core::HighwayNoteView tap_harmonic =
        noteWith(common::core::NoteAttack::Tap, common::core::LegatoMotion::Unjustified);
    tap_harmonic.harmonic_node = 12.0;
    CHECK(highwayNodeHead(tap_harmonic));

    // A pinch's node is the picking hand's graze over the body; its head stays on the stop and
    // keeps the family rectangle.
    common::core::HighwayNoteView pinch =
        noteWith(common::core::NoteAttack::Pinch, common::core::LegatoMotion::Unjustified);
    pinch.harmonic_node = 17.0;
    CHECK_FALSE(highwayNodeHead(pinch));

    // A node past the drawn board still draws AS a node (capped to the last fret), so it keeps
    // the round base.
    common::core::HighwayNoteView far_node =
        noteWith(common::core::NoteAttack::Pick, common::core::LegatoMotion::Unjustified);
    far_node.harmonic_node = 40.0;
    CHECK(highwayNodeHead(far_node));
}

} // namespace rock_hero::common::ui
